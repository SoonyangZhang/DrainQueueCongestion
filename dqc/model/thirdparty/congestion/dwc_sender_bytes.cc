#include "dwc_sender_bytes.h"
#include "rtt_stats.h"
#include "logging.h"
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <string>
#include "couple_cc_manager.h"
#include <iostream>
namespace dqc{

namespace {
// Maximum window to allow when doing bandwidth resumption.
const QuicPacketCount kMaxResumptionCongestionWindow = 200;
// Constants based on TCP defaults.
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const float kRenoBeta = 0.7f;  // Reno backoff factor 0.7 in quic.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
static const int alpha_scale = 10;
const TimeDelta kRttObservationExpiry = TimeDelta::FromSeconds(5);
const float kRttBeta = 0.3f; // defined in dwc paper
}  // namespace
static inline uint64_t mptcp_ccc_scale(uint32_t val, int scale)
{
	return (uint64_t) val << scale;
}
DwcSender::DwcSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
    bool reno,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    : rtt_stats_(rtt_stats),
      stats_(stats),
      reno_(true),
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
      min_rtt_timestamp_(ProtoTime::Zero()),
      min_rtt_(TimeDelta::Infinite()),
      max_rtt_timestamp_(ProtoTime::Zero()),
      max_rtt_(TimeDelta::Zero()){}

DwcSender::~DwcSender() {}
void DwcSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}

float DwcSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}

void DwcSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
  if (rtt_updated && InSlowStart() &&
      hybrid_slow_start_.ShouldExitSlowStart(
          rtt_stats_->latest_rtt(), rtt_stats_->min_rtt(),
          GetCongestionWindow() / kDefaultTCPMSS)) {
    ExitSlowstart();
  }
  if(event_time-max_rtt_timestamp_>kRttObservationExpiry){
    min_rtt_=TimeDelta::Infinite();
    max_rtt_=TimeDelta::Zero();
  }
  TimeDelta latest_rtt=rtt_stats_->latest_rtt();
  TimeDelta smooth_rtt=rtt_stats_->smoothed_rtt();
  if(min_rtt_>latest_rtt){
    min_rtt_=latest_rtt;
    min_rtt_timestamp_=event_time;
  }
  if(smooth_rtt>max_rtt_){
      max_rtt_=smooth_rtt;
      max_rtt_timestamp_=event_time;
  }
  for (const LostPacket& lost_packet : lost_packets) {
    OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                 prior_in_flight);
  }
  for (const AckedPacket acked_packet : acked_packets) {
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                  prior_in_flight, event_time);
  }
}

void DwcSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
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
  MaybeIncreaseCwnd(acked_packet_number, acked_bytes, prior_in_flight,
                    event_time);
  if (InSlowStart()) {
    hybrid_slow_start_.OnPacketAcked(acked_packet_number);
  }
}

void DwcSender::OnPacketSent(
    ProtoTime /*sent_time*/,
    QuicByteCount /*bytes_in_flight*/,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    HasRetransmittableData is_retransmittable) {
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
  hybrid_slow_start_.OnPacketSent(packet_number);
}

bool DwcSender::CanSend(QuicByteCount bytes_in_flight) {
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

QuicBandwidth DwcSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (no_prr_ && InRecovery() ? 1 : 1.25));
}

QuicBandwidth DwcSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool DwcSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool DwcSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool DwcSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool DwcSender::ShouldSendProbingPacket() const {
  return false;
}

void DwcSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

std::string DwcSender::GetDebugState() const {
  return "";
}

void DwcSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void DwcSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void DwcSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void DwcSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void DwcSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void DwcSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void DwcSender::OnPacketLost(QuicPacketNumber packet_number,
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
  if (InSlowStart()) {
    ++stats_->slowstart_packets_lost;
  }

  if (!no_prr_) {
    prr_.OnPacketLost(prior_in_flight);
  }
  mptcp_ccc_recalc_alpha();
  // TODO(b/77268641): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else{
    congestion_window_ = congestion_window_ * RenoBeta();
    for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
        (*it)->DelayInspection();
    }
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  // Reset packet count from congestion avoidance mode. We start counting again
  // when we're out of recovery.
  num_acked_packets_ = 0;
  /*QUIC_DVLOG(1)*/DLOG(INFO)<< "Incoming loss; congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_;
}

QuicByteCount DwcSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount DwcSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void DwcSender::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time) {
  //QUIC_BUG_IF(InRecovery()) << "Never increase the CWND during recovery.";
  // Do not increase the congestion window unless the sender is close to using
  // the current window.
  if (!IsCwndLimited(prior_in_flight)) {
    return;
  }
  if (congestion_window_ >= max_congestion_window_) {
    return;
  }
  if (InSlowStart()) {
    // TCP slow start, exponential growth, increase by one for each ACK.
    congestion_window_ += kDefaultTCPMSS;
    /*QUIC_DVLOG(1)*/DLOG(INFO) << "Slow start; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
    return;
  }
  ++num_acked_packets_;
  if(!other_ccs_.empty()){
	  bool subflows_exit_slow_start=true;
	  for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		  SendAlgorithmInterface *cc=(*it);
		  if(cc->InSlowStart()){
			  subflows_exit_slow_start=false;
			  break;
		  }
	  }
	  if(subflows_exit_slow_start){
		  mptcp_ccc_recalc_alpha();
		  int send_cwnd=0;
          //add by zsy  to avoid unreasonabele  alpha;
          // by assuming the multipath session has same rtt subflow.
          if(dwc_mode_){
              send_cwnd=mptcp_ccc_scale(1, alpha_scale)/alpha_;
              //send_cwnd=std::min(send_cwnd,limit);
          }else{
            size_t flows=1+other_ccs_.size();
            int limit=flows*(congestion_window_/kDefaultTCPMSS);
            send_cwnd=limit;
          }
          
		  if(send_cwnd<congestion_window_ / kDefaultTCPMSS){
			  send_cwnd=congestion_window_ / kDefaultTCPMSS;
		  }
		  if(num_acked_packets_*num_connections_>=send_cwnd){
			  congestion_window_ += kDefaultTCPMSS;
			  num_acked_packets_=0;
		  }
	  }else{
		    if (num_acked_packets_ * num_connections_ >=
		        congestion_window_ / kDefaultTCPMSS) {
		      congestion_window_ += kDefaultTCPMSS;
		      num_acked_packets_ = 0;
		    }
	  }
  }else{
	    if (num_acked_packets_ * num_connections_ >=
	        congestion_window_ / kDefaultTCPMSS) {
	      congestion_window_ += kDefaultTCPMSS;
	      num_acked_packets_ = 0;
	    }
  }
  DLOG(INFO)<< "Lia congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_
                << " congestion window count: " << num_acked_packets_;

}

void DwcSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void DwcSender::OnConnectionMigration() {
  hybrid_slow_start_.Restart();
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

CongestionControlType DwcSender::GetCongestionControlType() const {
  return kDwcBytes;
}
void DwcSender::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
	congestion_id_=cid;
	CoupleManager::Instance()->OnCongestionCreate(this);
}
void DwcSender::RegisterCoupleCC(SendAlgorithmInterface*cc){
	bool exist=false;
    DwcSender *sender=dynamic_cast<DwcSender*>(cc);
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		if(sender==(*it)){
			exist=true;
			break;
		}
	}
	if(!exist){
		other_ccs_.push_back(sender);
	}
}
void DwcSender::UnRegisterCoupleCC(SendAlgorithmInterface*cc){
    DwcSender *sender=dynamic_cast<DwcSender*>(cc);
	if(!other_ccs_.empty()){
		other_ccs_.remove(sender);
	}
}
TimeDelta DwcSender::get_smoothed_rtt() const{
	return rtt_stats_->smoothed_rtt();
}
void DwcSender::DelayInspection(){
	if(!dwc_mode_){
        return;
    }
    if(min_rtt_timestamp_==max_rtt_timestamp_){
        return;
    }
    if(InSlowStart()){
        return;
    }
    if(InRecovery()){
        return ;
    }
    TimeDelta srtt=rtt_stats_->smoothed_rtt();
    TimeDelta rtt_th=max_rtt_-(max_rtt_-min_rtt_)*kRttBeta;
    bool shoud_window_yield=srtt>rtt_th;
    if(shoud_window_yield){
        if (!no_prr_) {
        prr_.OnPacketLost(congestion_window_);
        }
        mptcp_ccc_recalc_alpha();
        congestion_window_ = congestion_window_ * RenoBeta();
        if (congestion_window_ < min_congestion_window_) {
            congestion_window_ = min_congestion_window_;
        }
        slowstart_threshold_ = congestion_window_;
        largest_sent_at_last_cutback_=largest_sent_packet_number_;
        num_acked_packets_ = 0;
        //std::cout<<congestion_id_<<" yes "<<std::endl;
    }
}
void DwcSender::mptcp_ccc_recalc_alpha(){
	uint64_t alpha = 1;
	uint64_t send_cwnd=GetCongestionWindow()/kDefaultTCPMSS;
    uint64_t sum_cwnd=send_cwnd;
    size_t flows=1+other_ccs_.size();
	TimeDelta srtt=get_smoothed_rtt();
	TimeDelta best_rtt=srtt;
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		DwcSender *sender=(*it);
        send_cwnd=sender->GetCongestionWindow()/kDefaultTCPMSS;
		srtt=sender->get_smoothed_rtt();
        sum_cwnd+=send_cwnd;
		if(best_rtt>srtt){
			best_rtt=srtt;
		}
	}
	srtt=get_smoothed_rtt();
	alpha=mptcp_ccc_scale(1,alpha_scale) *srtt.ToMicroseconds()
    /(flows*sum_cwnd*best_rtt.ToMicroseconds());
	CHECK(alpha>=1);
	alpha_=alpha;
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		DwcSender *sender=(*it);
		sender->set_alpha(alpha);
	}
}
}
