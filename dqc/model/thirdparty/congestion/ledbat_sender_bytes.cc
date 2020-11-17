#include "ledbat_sender_bytes.h"
#include "unacked_packet_map.h"
#include "rtt_stats.h"
#include "logging.h"
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <string>
#include <iostream>
namespace dqc{

namespace {
// Maximum window to allow when doing bandwidth resumption.
const QuicPacketCount kMaxResumptionCongestionWindow = 200;
// Constants based on TCP defaults.
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const float kRenoBeta = 0.5f;
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
const TimeDelta kInitialTargetDelay=TimeDelta::FromMilliseconds(100);
const double kOffsetGain=1.0;
const uint32_t kBaseHistoryLen=10;
const uint32_t kNoiseFilterLen=4;
}  // namespace
LedbatSender::LedbatSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
    const UnackedPacketMap* unacked_packets,
    uint8_t do_ss,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    : rtt_stats_(rtt_stats),
    unacked_packets_(unacked_packets),
    stats_(stats),
    num_connections_(kDefaultNumConnections),
    min4_mode_(false),
    last_cutback_exited_slowstart_(false),
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
    target_(kInitialTargetDelay),
    ledbat_gain_(kOffsetGain),
    doSs_(0),
    baseHistoLen_(kBaseHistoryLen),
    noiseFilterLen_(kNoiseFilterLen),
    lastRollover_(ProtoTime::Zero()),
    snd_cwnd_cnt_(0)
    {
		ledbat_flag_=LEDBAT_INVALID_FLAG;
        if(doSs_>0){
            ledbat_flag_|=LEDBAT_CAN_SS;
        }
        InitCircBuf(baseHistory_);
        InitCircBuf(noiseFilter_);
    }

LedbatSender::~LedbatSender() {}
void LedbatSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}
float LedbatSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}
void LedbatSender::OnOneWayDelaySample(ProtoTime event_time,QuicPacketNumber seq,
            ProtoTime sent_time,ProtoTime recv_time){
        if((recv_time>ProtoTime::Zero())&&(recv_time>sent_time)){
            if(lastRollover_==ProtoTime::Zero()){
                lastRollover_=event_time;
            }
            TimeDelta owd=recv_time-sent_time;
            AddDelay(noiseFilter_,owd,noiseFilterLen_);
            UpdateBaseDelay(event_time,owd);
            ledbat_flag_|=LEDBAT_VALID_OWD;
        }else{
            ledbat_flag_&= ~LEDBAT_VALID_OWD;
        }
}
void LedbatSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
    if(!(ledbat_flag_&LEDBAT_VALID_OWD)){
        return ;
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

void LedbatSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                        QuicByteCount acked_bytes,
                                        QuicByteCount prior_in_flight,
                                        ProtoTime event_time) {
  largest_acked_packet_number_.UpdateMax(acked_packet_number);
  
    if(congestion_window_<=min_congestion_window_){
        ledbat_flag_|= LEDBAT_CAN_SS;
		slowstart_threshold_=max_congestion_window_;
    }
    
    if(doSs_>=DO_SLOWSTART&&InSlowStart()){
        congestion_window_ += kDefaultTCPMSS;
        return;
    }else{
        ledbat_flag_&=~LEDBAT_CAN_SS;
    }
    //uint32_t send_cwnd=congestion_window_/kDefaultTCPMSS;
    //uint32_t max_cwnd=send_cwnd*target_.ToMilliseconds();
    TimeDelta current_delay=CurrentDelay(&LedbatSender::MinCircBuf);
    TimeDelta base_delay=BaseDelay();
    int64_t queue_delay;
    int64_t offset=0;
    if(current_delay>=base_delay){
        queue_delay=current_delay.ToMilliseconds()-base_delay.ToMilliseconds();
        offset=target_.ToMilliseconds()-queue_delay;
    }else{
        queue_delay=base_delay.ToMilliseconds()-current_delay.ToMilliseconds();
        offset=target_.ToMilliseconds()+queue_delay;
    }
	if(offset>target_.ToMilliseconds()){
		offset=target_.ToMilliseconds();
	}
    snd_cwnd_cnt_=static_cast<int32_t> (offset *kDefaultTCPMSS);
    double inc=(snd_cwnd_cnt_*1.0)/(target_.ToMilliseconds()*congestion_window_);
    if(inc>0){
        QuicByteCount addon=num_connections_*inc*kDefaultTCPMSS;
        congestion_window_+=addon;
    }else{
        inc=-inc;
        QuicByteCount addon=inc*kDefaultTCPMSS;
        addon=std::min(congestion_window_,addon);
        congestion_window_-=addon;
    }
    //from https://github.com/silviov/TCP-LEDBAT/blob/master/src/tcp_ledbat.c
    /*TimeDelta queue_delay=current_delay-base_delay;
    TimeDelta offset=target_-queue_delay;
    if(offset>target_){
        offset=target_;
    }
    int64_t cwnd=snd_cwnd_cnt_+offset.ToMilliseconds();
    if(cwnd>=0){
        snd_cwnd_cnt_=cwnd;
        if(snd_cwnd_cnt_>=max_cwnd){
            congestion_window_ += kDefaultTCPMSS;
            snd_cwnd_cnt_=0;
        }
    }else{
        if(congestion_window_>min_congestion_window_){
            congestion_window_-=kDefaultTCPMSS;
            send_cwnd=congestion_window_/kDefaultTCPMSS;
            snd_cwnd_cnt_=(send_cwnd-1)*target_.ToMilliseconds();
        }
    }*/
    congestion_window_ = std::max(congestion_window_, min_congestion_window_);
    congestion_window_ = std::min(congestion_window_, max_congestion_window_); 
}
void LedbatSender::OnPacketSent(
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
  DCHECK(!largest_sent_packet_number_.IsInitialized() ||
         largest_sent_packet_number_ < packet_number);
  largest_sent_packet_number_ = packet_number;
}

bool LedbatSender::CanSend(QuicByteCount bytes_in_flight) {
  if (GetCongestionWindow() > bytes_in_flight) {
    return true;
  }
  if (min4_mode_ && bytes_in_flight < 4 * kDefaultTCPMSS) {
    return true;
  }
  return false;
}

QuicBandwidth LedbatSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (InRecovery() ? 1 : 1.25));
}

QuicBandwidth LedbatSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool LedbatSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool LedbatSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool LedbatSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool LedbatSender::ShouldSendProbingPacket() const {
  return false;
}

void LedbatSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  HandleRetransmissionTimeout();
}

std::string LedbatSender::GetDebugState() const {
  return "";
}

void LedbatSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void LedbatSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void LedbatSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void LedbatSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void LedbatSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void LedbatSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}
void LedbatSender::OnPacketLost(QuicPacketNumber packet_number,
                                QuicByteCount lost_bytes,
                                QuicByteCount prior_in_flight) {
  // TCP NewReno (RFC6582) says that once a loss occurs, any losses in packets
  // already sent should be treated as a single loss event, since it's expected.
  if (largest_sent_at_last_cutback_.IsInitialized() &&
      packet_number <= largest_sent_at_last_cutback_) {
    if (last_cutback_exited_slowstart_) {
      ++stats_->slowstart_packets_lost;
      stats_->slowstart_bytes_lost += lost_bytes;
    }
    return;
  }
  ++stats_->tcp_loss_events;
  last_cutback_exited_slowstart_ = InSlowStart();
  if (InSlowStart()) {
    ++stats_->slowstart_packets_lost;
  }
  congestion_window_ =congestion_window_*RenoBeta();
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
}
QuicByteCount LedbatSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount LedbatSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}
void LedbatSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}
void LedbatSender::OnConnectionMigration() {
  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType LedbatSender::GetCongestionControlType() const {
  return kLedbat;
}
void LedbatSender::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
}
void LedbatSender::InitCircBuf (struct OwdCircBuf &buffer){
  buffer.buffer.clear ();
  buffer.min = 0;
}
TimeDelta LedbatSender::MinCircBuf (struct OwdCircBuf &b){
  if (b.buffer.size () == 0)
    {
      return TimeDelta::Infinite();
    }
  else
    {
      return b.buffer[b.min];
    }
}
TimeDelta LedbatSender::CurrentDelay (FilterFunction filter)
{
  return filter (noiseFilter_);
}
TimeDelta LedbatSender::BaseDelay ()
{
  return MinCircBuf (baseHistory_);
}
void LedbatSender::AddDelay (struct OwdCircBuf &cb, TimeDelta owd, uint32_t maxlen)
{
  if (cb.buffer.size () == 0)
    {
      cb.buffer.push_back (owd);
      cb.min = 0;
      return;
    }
  cb.buffer.push_back (owd);
  if (cb.buffer[cb.min] > owd)
    {
      cb.min = static_cast<uint32_t> (cb.buffer.size () - 1);
    }
  if (cb.buffer.size () >= maxlen)
    {
      cb.buffer.erase (cb.buffer.begin ());
      cb.min = 0;
      for (uint32_t i = 1; i < maxlen - 1; i++)
        {
          if (cb.buffer[i] < cb.buffer[cb.min])
            {
              cb.min = i;
            }
        }
    }
}
void LedbatSender::UpdateBaseDelay (ProtoTime event_time,TimeDelta owd){
  if (baseHistory_.buffer.size () == 0)
    {
      AddDelay (baseHistory_, owd, baseHistoLen_);
      return;
    }
  if (event_time - lastRollover_>TimeDelta::FromSeconds(60))
    {
      lastRollover_ = event_time;
      AddDelay (baseHistory_, owd, baseHistoLen_);
    }
  else
    {
      uint32_t last = static_cast<uint32_t> (baseHistory_.buffer.size () - 1);
      if (owd < baseHistory_.buffer[last])
        {
          baseHistory_.buffer[last] = owd;
          if (owd < baseHistory_.buffer[baseHistory_.min])
            {
              baseHistory_.min = last;
            }
        }
    }
}
}
