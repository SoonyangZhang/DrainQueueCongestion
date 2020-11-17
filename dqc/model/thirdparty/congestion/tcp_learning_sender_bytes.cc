#include <algorithm>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <memory.h>
#include "tcp_learning_sender_bytes.h"
#include "unacked_packet_map.h"
#include "flag_impl.h"
#include "flag_util_impl.h"
#include "logging.h"
#include "random.h"
#include "ns3/log.h"
using namespace ns3;
namespace dqc{
NS_LOG_COMPONENT_DEFINE("tcp-learn");
namespace {
// Maximum window to allow when doing bandwidth resumption.
const QuicPacketCount kMaxResumptionCongestionWindow = 200;
// Constants based on TCP defaults.
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const float kRateBeta = 0.9f; 
const float kRenoBeta = 0.5f;
const TimeDelta kMinRttExpiry = TimeDelta::FromMilliseconds(10000);
const QuicByteCount kDefaultMinimumCongestionWindow = 4* kDefaultTCPMSS;
const float kDerivedHighCWNDGain = 2.0f;
const float kBandwidthWestwoodAlpha = 0.125f;
const QuicRoundTripCount kBandwidthWindowSize=10;
const float kSimilarMinRttThreshold = 1.125;
const float kStartupGrowthTarget = 1.5;
const float kStartupAfterLossGain = 1.5f;
const QuicRoundTripCount kRoundTripsWithoutGrowthBeforeExitingStartup = 3;
const float kLatencyFactor=0.8;
const QuicRoundTripCount kMaxRttObservationWindow=20;
const uint32_t kActionTable[kActionTableSize]={1,2,3,4,5};
const double kLearningRate=0.3;
const float kRewardAlpha=0.85;
const float kRewardOneMinusAlpha=1-kRewardAlpha;
}
ActionTrace* ActionTrace::Instance(){
    static ActionTrace * const ins=new ActionTrace();
    return ins;
}
void ActionTrace::Destruct(){
    if(traces_.is_open()){
        traces_.flush();
        traces_.close();
    }
}
void ActionTrace::RecordAction(uint32_t millis,uint32_t cid,uint32_t action){
    if(traces_.is_open()){
        traces_<<millis<<"\t"<<cid<<"\t"<<action<<std::endl;
    }
}
ActionTrace::ActionTrace(){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+"learning_act.txt";
	traces_.open(path.c_str(), std::fstream::out);    
}
ActionTrace::~ActionTrace(){
    
}
Reward::Reward()
:first_(true),value_(0){}
Reward::~Reward(){}
void Reward::UpdateReward(double new_sample){
    if(first_){
        first_=false;
        value_=new_sample;
    }else{
        value_=kRewardOneMinusAlpha*value_+kRewardAlpha*new_sample;
    }
}
void Reward::Reset(){
    first_=true;
    value_=0;
}
TcpLearningSenderBytes::TcpLearningSenderBytes(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
    const UnackedPacketMap* unacked_packets,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats,Random* random,bool half)
    :clock_(clock),
    rtt_stats_(rtt_stats),
    unacked_packets_(unacked_packets),
    stats_(stats),
    random_(random),
    half_cwnd_on_loss_(half),
    num_connections_(kDefaultNumConnections),
    min4_mode_(false),
    last_cutback_exited_slowstart_(false),
    slow_start_large_reduction_(false),
    no_prr_(false),
    num_acked_packets_(0),
    congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
    min_congestion_window_(kDefaultMinimumCongestionWindow),
    max_congestion_window_(max_congestion_window * kDefaultTCPMSS),
    slowstart_threshold_(max_congestion_window * kDefaultTCPMSS),
    initial_tcp_congestion_window_(initial_tcp_congestion_window *
                                   kDefaultTCPMSS),
    initial_max_tcp_congestion_window_(max_congestion_window *
                                       kDefaultTCPMSS),
    min_slow_start_exit_window_(min_congestion_window_),
    reset_rtt_min_(true),
    mode_(STARTUP),
    round_trip_count_(0),
    max_bandwidth_(kBandwidthWindowSize, QuicBandwidth::Zero(), 0),
    high_gain_(kDerivedHighCWNDGain/*kDefaultHighGain*/),
    high_cwnd_gain_(kDerivedHighCWNDGain/*kDefaultHighGain*/),
    drain_gain_(1.f /kDerivedHighCWNDGain/*kDefaultHighGain*/),
    pacing_rate_(QuicBandwidth::Zero()),
    pacing_gain_(1),
    congestion_window_gain_(1),
    num_startup_rtts_(kRoundTripsWithoutGrowthBeforeExitingStartup),
    is_at_full_bandwidth_(false),
    rounds_without_bandwidth_gain_(0),
    bandwidth_at_last_round_(QuicBandwidth::Zero()),
    min_rtt_since_last_probe_rtt_(TimeDelta::Infinite()),
    startup_rate_reduction_multiplier_(0),
    startup_bytes_lost_(0),
    always_get_bw_sample_when_acked_(
    GetQuicReloadableFlag(quic_always_get_bw_sample_when_acked)),
    min_rtt_(TimeDelta::Zero()),
    min_rtt_timestamp_(ProtoTime::Zero()),
	base_rtt_(TimeDelta::Infinite()),
    probe_rtt_skipped_if_similar_rtt_(false),
    exit_startup_on_loss_(false),
    max_rtt_(kMaxRttObservationWindow,TimeDelta::Zero(),0){
        EnterStartupMode(clock->Now());
    }

TcpLearningSenderBytes::~TcpLearningSenderBytes() {
	dqc::ActionTrace::Instance()->Destruct();
}
void TcpLearningSenderBytes::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }
  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}
void TcpLearningSenderBytes::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
    bool is_round_start = false;
    bool min_rtt_expired = false;
    QuicBandwidth bandwidth=QuicBandwidth::Zero();
    DiscardLostPackets(lost_packets);
    TimeDelta srtt = rtt_stats_->smoothed_rtt();
    TimeDelta rtt=rtt_stats_->latest_rtt();
    QuicPacketNumber last_acked_packet;
    if (!acked_packets.empty()) {
	    last_acked_packet = acked_packets.rbegin()->packet_number;
	    is_round_start = UpdateRoundTripCounter(last_acked_packet);
	    min_rtt_expired = UpdateBandwidthAndMinRtt(event_time, acked_packets,bandwidth);
        max_rtt_.Update(srtt,round_trip_count_);
        if(min_rtt_expired){
            reset_rtt_min_=true;
        }
    }
    if(mode_==AIMD){
        if(base_rtt_>rtt){
            base_rtt_=rtt;
        }
        srtt_monitor_.UpdateRtt(rtt,TimeDelta::Zero(),event_time);
        CHECK(last_acked_packet.IsInitialized());
        if(last_acked_packet>=largest_sent_at_new_action_){
            if(!largest_sent_when_action_sent_acked_.IsInitialized()){
                largest_sent_when_action_sent_acked_=largest_sent_packet_number_;
            }
        }
        if(largest_sent_when_action_sent_acked_.IsInitialized()&&
        (last_acked_packet>largest_sent_when_action_sent_acked_)){
            double reward=double(bandwidth.ToKBitsPerSecond())/
            srtt_monitor_.smoothed_rtt().ToMilliseconds();
            reward_table_[action_index_].UpdateReward(reward);
        }
        bool rtt_under_delay_threshold{false};
        if(rtt<GetDelayThreshold()){
            rtt_under_delay_threshold=true;
        }
        if(!rtt_under_delay_threshold){
            CongestionWindowBackoff(last_acked_packet,prior_in_flight,kRateBeta);
        }
		if(min_rtt_expired){
			CongestionWindowBackoff(last_acked_packet,prior_in_flight,kRateBeta);
		}    
        for (const LostPacket& lost_packet : lost_packets) {
        OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                 prior_in_flight);
        }
        for (const AckedPacket acked_packet : acked_packets) {
        OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                  prior_in_flight, event_time);
        }        
    }else{
        if (is_round_start && !is_at_full_bandwidth_) {
            CheckIfFullBandwidthReached();
        }
        MaybeExitStartupOrDrain(event_time);
        CalculatePacingRate();
        CalculateCongestionWindow();
    }
    sampler_.RemoveObsoletePackets(unacked_packets_->GetLeastUnacked());
}
void TcpLearningSenderBytes::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                        QuicByteCount acked_bytes,
                                        QuicByteCount prior_in_flight,
                                        ProtoTime event_time) {
  largest_acked_packet_number_.UpdateMax(acked_packet_number);
  if (InRecovery()) {
    if (!no_prr_) {
      // PRR is used when in recovery.
      prr_.OnPacketAcked(acked_bytes);
    }
    return;
  }
  if(!has_action_chosen_){
    base_rtt_=TimeDelta::Infinite();
    srtt_monitor_.Reset();
    ChooseAction();
  }
  MaybeIncreaseCwnd(acked_packet_number, acked_bytes, prior_in_flight,
                    event_time);
}

void TcpLearningSenderBytes::OnPacketSent(
    ProtoTime sent_time,
    QuicByteCount bytes_in_flight,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    HasRetransmittableData is_retransmittable){
  if (InSlowStart()) {
    ++(stats_->slowstart_packets_sent);
  }

  if (is_retransmittable != HAS_RETRANSMITTABLE_DATA) {
    return;
  }
  if (InRecovery()) {
    // PRR is used when in recovery.
    prr_.OnPacketSent(bytes);
  }
  DCHECK(!largest_sent_packet_number_.IsInitialized() ||
         largest_sent_packet_number_ < packet_number);
  largest_sent_packet_number_ = packet_number;
  sampler_.OnPacketSent(sent_time, packet_number, bytes, bytes_in_flight,
                        is_retransmittable);
}

bool TcpLearningSenderBytes::CanSend(QuicByteCount bytes_in_flight) {
  if (!no_prr_ && InRecovery()) {
    // PRR is used when in recovery.
    return prr_.CanSend(GetCongestionWindow(), bytes_in_flight,
                        GetSlowStartThreshold());
  }
  if (GetCongestionWindow() > bytes_in_flight) {
    return true;
  }
  if (min4_mode_ && bytes_in_flight < 4 * kDefaultTCPMSS) {
    return true;
  }
  return false;
}

QuicBandwidth TcpLearningSenderBytes::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  if (pacing_rate_.IsZero()) {
    return high_gain_ * QuicBandwidth::FromBytesAndTimeDelta(
                            initial_tcp_congestion_window_, GetMinRtt());
  }
  if(mode_==AIMD){
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * ((no_prr_ && InRecovery() ? 1 : 1.25));
  }else{
      return pacing_rate_;
  }
}
QuicBandwidth TcpLearningSenderBytes::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if(mode_==STARTUP||mode_==DRAIN){
      return BandwidthEstimateBest();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}
QuicBandwidth TcpLearningSenderBytes::BandwidthEstimateBest() const {
    return max_bandwidth_.GetBest();
}
bool TcpLearningSenderBytes::InSlowStart() const {
  return mode_ == STARTUP||mode_==DRAIN;
}

bool TcpLearningSenderBytes::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool TcpLearningSenderBytes::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool TcpLearningSenderBytes::ShouldSendProbingPacket() const {
  return false;
}

void TcpLearningSenderBytes::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  HandleRetransmissionTimeout();
}

std::string TcpLearningSenderBytes::GetDebugState() const {
  return "";
}

void TcpLearningSenderBytes::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void TcpLearningSenderBytes::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void TcpLearningSenderBytes::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void TcpLearningSenderBytes::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void TcpLearningSenderBytes::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}
void TcpLearningSenderBytes::CongestionWindowBackoff(QuicPacketNumber packet_number,QuicByteCount prior_in_flight,float gain){
  if (largest_sent_at_last_cutback_.IsInitialized() &&
      packet_number <= largest_sent_at_last_cutback_) {
          return ;
    }
  if (!no_prr_) {
    prr_.OnPacketLost(prior_in_flight);
  }
  QuicBandwidth bw_est=BandwidthEstimateBest();
  if(bw_est.IsZero()){
      congestion_window_=congestion_window_*RenoBeta();
  }else{
      congestion_window_=gain*bw_est*min_rtt_;
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  num_acked_packets_ = 0;
  has_action_chosen_=false;
}
void TcpLearningSenderBytes::OnPacketLost(QuicPacketNumber packet_number,
                                       QuicByteCount lost_bytes,
                                       QuicByteCount prior_in_flight) {
  // TCP NewReno (RFC6582) says that once a loss occurs, any losses in packets
  // already sent should be treated as a single loss event, since it's expected.
  if (largest_sent_at_last_cutback_.IsInitialized() &&
      packet_number <= largest_sent_at_last_cutback_) {
    if (last_cutback_exited_slowstart_) {
      ++stats_->slowstart_packets_lost;
      stats_->slowstart_bytes_lost += lost_bytes;
      if (slow_start_large_reduction_) {
        // Reduce congestion window by lost_bytes for every loss.
        congestion_window_ = std::max(congestion_window_ - lost_bytes,
                                      min_slow_start_exit_window_);
        slowstart_threshold_ = congestion_window_;
      }
    }
    /*QUIC_DVLOG(1)*/DLOG(INFO)<< "Ignoring loss for largest_missing:" << packet_number
                  << " because it was sent prior to the last CWND cutback.";
    return;
  }
  ++stats_->tcp_loss_events;
  last_cutback_exited_slowstart_ = InSlowStart();
  if (!no_prr_) {
    prr_.OnPacketLost(prior_in_flight);
  }

  // TODO(b/77268641): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else{
        QuicBandwidth bw_est=BandwidthEstimateBest();
        if(bw_est.IsZero()||half_cwnd_on_loss_){
            congestion_window_=congestion_window_*RenoBeta();
        }else{
            QuicBandwidth  rate=BandwidthEstimateBest();
            congestion_window_=kRateBeta*rate*min_rtt_;
        }          
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  num_acked_packets_ = 0;
  has_action_chosen_=false;
}

QuicByteCount TcpLearningSenderBytes::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount TcpLearningSenderBytes::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void TcpLearningSenderBytes::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time) {
    if (!IsCwndLimited(prior_in_flight)) {
        return;
    }
    if (congestion_window_ >= max_congestion_window_) {
        return;
    }
    ++num_acked_packets_;
    uint32_t action=kActionTable[action_index_];
    if (num_acked_packets_ * action>=
        congestion_window_ / kDefaultTCPMSS) {
    congestion_window_ += kDefaultTCPMSS;
    num_acked_packets_ = 0;
    }
}

void TcpLearningSenderBytes::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}
void TcpLearningSenderBytes::EnterStartupMode(ProtoTime now) {
  mode_ = STARTUP;
  pacing_gain_ = high_gain_;
  congestion_window_gain_ = high_cwnd_gain_;
}

void TcpLearningSenderBytes::EnterAIMDMode(ProtoTime now) {
  mode_ = AIMD;
  congestion_window_gain_ =1.0;
  congestion_window_=GetTargetCongestionWindow(congestion_window_gain_);
  base_rtt_=TimeDelta::Infinite();
  srtt_monitor_.Reset();
  ExploreAction();
  //uint32_t milli=(now-ProtoTime::Zero()).ToMilliseconds();
 // ActionTrace::Instance()->RecordAction(milli,congestion_id_,action_index_);
}
void TcpLearningSenderBytes::DiscardLostPackets(const LostPacketVector& lost_packets) {
  for (const LostPacket& packet : lost_packets) {
    sampler_.OnPacketLost(packet.packet_number);
    if (mode_ == STARTUP) {
      if (startup_rate_reduction_multiplier_ != 0) {
        startup_bytes_lost_ += packet.bytes_lost;
      }
    }
  }
}
bool TcpLearningSenderBytes::UpdateRoundTripCounter(QuicPacketNumber last_acked_packet) {
  if (!current_round_trip_end_.IsInitialized()||
      last_acked_packet > current_round_trip_end_) {
    round_trip_count_++;
    current_round_trip_end_ =largest_sent_packet_number_;
    return true;
  }
  return false;
}
bool TcpLearningSenderBytes::UpdateBandwidthAndMinRtt(
    ProtoTime now,
    const AckedPacketVector& acked_packets,QuicBandwidth &bandwidth) {
  TimeDelta sample_min_rtt = TimeDelta::Infinite();
  bandwidth=QuicBandwidth::Zero();
  for (const auto& packet : acked_packets) {
    if (!always_get_bw_sample_when_acked_ && packet.bytes_acked == 0) {
      // Skip acked packets with 0 in flight bytes when updating bandwidth.
      continue;
    }
    BandwidthSample bandwidth_sample =
        sampler_.OnPacketAcknowledged(now, packet.packet_number);
    if (always_get_bw_sample_when_acked_ &&
        !bandwidth_sample.state_at_send.is_valid) {
      // From the sampler's perspective, the packet has never been sent, or the
      // packet has been acked or marked as lost previously.
      continue;
    }
    if (!bandwidth_sample.rtt.IsZero()) {
      sample_min_rtt = std::min(sample_min_rtt, bandwidth_sample.rtt);
    }
    bandwidth=std::max(bandwidth,bandwidth_sample.bandwidth);
    max_bandwidth_.Update(bandwidth_sample.bandwidth, round_trip_count_);
  }

  // If none of the RTT samples are valid, return immediately.
  if (sample_min_rtt.IsInfinite()) {
    return false;
  }
  if(reset_rtt_min_){
      min_rtt_timestamp_=now;
      min_rtt_=sample_min_rtt;
      reset_rtt_min_=false;
      return false;
  }
  min_rtt_since_last_probe_rtt_ =
      std::min(min_rtt_since_last_probe_rtt_, sample_min_rtt);

  // Do not expire min_rtt if none was ever available.
  bool min_rtt_expired =
      !min_rtt_.IsZero() && (now > (min_rtt_timestamp_ + kMinRttExpiry));

  if (min_rtt_expired || sample_min_rtt < min_rtt_ || min_rtt_.IsZero()) {

    if (min_rtt_expired && ShouldExtendMinRttExpiry()) {
      min_rtt_expired = false;
    } else {
      min_rtt_ = sample_min_rtt;
    }
    min_rtt_timestamp_ = now;
    // Reset since_last_probe_rtt fields.
    min_rtt_since_last_probe_rtt_ = TimeDelta::Infinite();
  }
  return min_rtt_expired;
}
bool TcpLearningSenderBytes::ShouldExtendMinRttExpiry() const {
  const bool min_rtt_increased_since_last_probe =
      min_rtt_since_last_probe_rtt_ > min_rtt_ * kSimilarMinRttThreshold;
  if (probe_rtt_skipped_if_similar_rtt_ &&
      !min_rtt_increased_since_last_probe) {
    // Extend the current min_rtt if we've been app limited recently and an rtt
    // has been measured in that time that's less than 12.5% more than the
    // current min_rtt.
    return true;
  }
  return false;
}
void TcpLearningSenderBytes::CheckIfFullBandwidthReached(){
  QuicBandwidth target = bandwidth_at_last_round_ * kStartupGrowthTarget;
  if (BandwidthEstimateBest() >= target) {
    bandwidth_at_last_round_ = BandwidthEstimateBest();
    rounds_without_bandwidth_gain_ = 0;
    return;
  }
  rounds_without_bandwidth_gain_++;
  if ((rounds_without_bandwidth_gain_ >= num_startup_rtts_) ||
      (exit_startup_on_loss_ && InRecovery())) {
    is_at_full_bandwidth_ = true;
  }    
}
void TcpLearningSenderBytes::MaybeExitStartupOrDrain(ProtoTime now) {
  if (mode_ == STARTUP && is_at_full_bandwidth_) {
    mode_ = DRAIN;
    pacing_gain_ = drain_gain_;
    congestion_window_gain_ = high_cwnd_gain_;
  }
  if (mode_ == DRAIN &&
      unacked_packets_->bytes_in_flight() <= GetTargetCongestionWindow(1)) {
    EnterAIMDMode(now);
  }
}
void TcpLearningSenderBytes::CalculatePacingRate(){
  if (BandwidthEstimateBest().IsZero()) {
    return;
  }

  QuicBandwidth target_rate = pacing_gain_ * BandwidthEstimateBest();
  if (is_at_full_bandwidth_) {
    pacing_rate_ = target_rate;
    return;
  }
  if (pacing_rate_.IsZero() && !rtt_stats_->min_rtt().IsZero()) {
    pacing_rate_ = QuicBandwidth::FromBytesAndTimeDelta(
        initial_tcp_congestion_window_, rtt_stats_->min_rtt());
    return;
  }
  pacing_rate_ = std::max(pacing_rate_, target_rate);
}
void TcpLearningSenderBytes::CalculateCongestionWindow(){
  QuicByteCount target_window =
      GetTargetCongestionWindow(congestion_window_gain_);
  congestion_window_=target_window;
  congestion_window_ = std::max(congestion_window_, min_congestion_window_);
  congestion_window_ = std::min(congestion_window_, max_congestion_window_);
}
float TcpLearningSenderBytes::RenoBeta() const {
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}
TimeDelta TcpLearningSenderBytes::GetMinRtt() const {
  return !min_rtt_.IsZero() ? min_rtt_ : rtt_stats_->initial_rtt();
}

QuicByteCount TcpLearningSenderBytes::GetTargetCongestionWindow(float gain) const {
  QuicByteCount bdp = GetMinRtt() * BandwidthEstimateBest();
  QuicByteCount congestion_window = gain * bdp;

  // BDP estimate will be zero if no bandwidth samples are available yet.
  if (congestion_window == 0) {
    congestion_window = gain * initial_tcp_congestion_window_;
  }

  return std::max(congestion_window, min_congestion_window_);
}
void TcpLearningSenderBytes::OnConnectionMigration() {
  prr_ = PrrSender();
  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType TcpLearningSenderBytes::GetCongestionControlType() const {
  return kLearningBytes;
}
void TcpLearningSenderBytes::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
	congestion_id_=cid;
}
TimeDelta TcpLearningSenderBytes::GetDelayThreshold(){
    TimeDelta threshold=TimeDelta::Zero();
    TimeDelta max_rtt=max_rtt_.GetBest();
    if(max_rtt>base_rtt_){
        threshold=base_rtt_+kLatencyFactor*(max_rtt-base_rtt_);
    }
    return threshold;
}
void TcpLearningSenderBytes::ChooseAction(){
    double rand=random_->nextRealOpen();
    //uint32_t last_action_index=action_index_;
    if(rand<kLearningRate){
        ExploreAction();
    }else{
        ExploitAction();
    }
    /*if(last_action_index!=action_index_){
    uint32_t milli=(clock_->Now()-ProtoTime::Zero()).ToMilliseconds();
    ActionTrace::Instance()->RecordAction(milli,congestion_id_,action_index_);
    }*/
}
void TcpLearningSenderBytes::ExploitAction(){
    Reward max_reward=reward_table_[action_index_];
    uint32_t i=0;
    for(i=0;i<kActionTableSize;i++){
        if(reward_table_[i]>max_reward){
            max_reward=reward_table_[i];
            action_index_=i;
        }
    }
    largest_sent_at_new_action_=largest_sent_packet_number_;
    largest_sent_when_action_sent_acked_.Clear();
    has_action_chosen_=true;    
}
void TcpLearningSenderBytes::ExploreAction(){
    action_index_=random_->nextInt(0,kActionTableSize);
    largest_sent_at_new_action_=largest_sent_packet_number_;
    largest_sent_when_action_sent_acked_.Clear();
    has_action_chosen_=true;
}
}
