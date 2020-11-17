#include "lptcp_sender_bytes.h"
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
LpTcpSender::LpTcpSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
    const UnackedPacketMap* unacked_packets,
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
    min_slow_start_exit_window_(min_congestion_window_){
	owd_stats_.reset(new RttStats());
}

LpTcpSender::~LpTcpSender() {}
void LpTcpSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}
float LpTcpSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}
void LpTcpSender::OnOneWayDelaySample(ProtoTime event_time,QuicPacketNumber seq,
            ProtoTime sent_time,ProtoTime recv_time){
        if((recv_time>ProtoTime::Zero())&&(recv_time>sent_time)){
            TimeDelta owd=recv_time-sent_time;
            owd_stats_->UpdateRtt(owd,TimeDelta::Zero(),event_time);
            lp_flag_|=LP_VALID_OWD;
            RttSample(owd);
        }else{
            lp_flag_&= ~LP_VALID_OWD;
        }
}
void LpTcpSender::RttSample(TimeDelta owd){
    if(owd<min_owd_){
        min_owd_=owd;
    }
    if(owd>max_owd_){
        if(owd>max_owd_rsv_){
            if(max_owd_rsv_.IsZero()){
                max_owd_=owd;
            }else{
                max_owd_=max_owd_rsv_;
            }
            max_owd_rsv_=owd;
        }else{
            max_owd_=owd;
        }
    }
}
void LpTcpSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
    if(!(lp_flag_&LP_VALID_OWD)){
        return ;
    }
    if (rtt_updated && InSlowStart() &&
      hybrid_slow_start_.ShouldExitSlowStart(
          rtt_stats_->latest_rtt(), rtt_stats_->min_rtt(),
          GetCongestionWindow() / kDefaultTCPMSS)) {
    ExitSlowstart();
    }
    for (const LostPacket& lost_packet : lost_packets) {
    OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                prior_in_flight);
    }
    for (const AckedPacket acked_packet : acked_packets) {
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                    prior_in_flight, event_time);
    if(!(lp_flag_&LP_WITHIN_INF)){
        MaybeIncreaseCwnd(acked_packet.packet_number,acked_packet.bytes_acked,
                    prior_in_flight, event_time);
    }
    if (InSlowStart()){
    hybrid_slow_start_.OnPacketAcked(acked_packet.packet_number);
    }
    }
}

void LpTcpSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                        QuicByteCount acked_bytes,
                                        QuicByteCount prior_in_flight,
                                        ProtoTime event_time) {
    largest_acked_packet_number_.UpdateMax(acked_packet_number);
    TimeDelta rtt=rtt_stats_->latest_rtt();
    inference_=3*rtt;
    if(last_drop_.IsInitialized()&&(event_time-last_drop_<inference_)){
         lp_flag_|= LP_WITHIN_INF;
    }else{
        lp_flag_&=~LP_WITHIN_INF;
    }
    int64_t micro_second=15*(max_owd_.ToMicroseconds()-min_owd_.ToMicroseconds())/100;
    int64_t delay_threshold=min_owd_.ToMicroseconds()+micro_second;
    int64_t sowd=owd_stats_->smoothed_rtt().ToMicroseconds();
    
    if((sowd>>3)<=delay_threshold){
        lp_flag_|= LP_WITHIN_THR;
    }else{
        lp_flag_ &= ~LP_WITHIN_THR;
    }
    if(lp_flag_&LP_WITHIN_THR){
        return;
    }
    min_owd_=TimeDelta::FromMicroseconds(sowd>>3);
    max_owd_=TimeDelta::FromMicroseconds(sowd>>2);
    max_owd_rsv_=TimeDelta::FromMicroseconds(sowd>>2);
    if(lp_flag_&LP_WITHIN_INF){
        congestion_window_=min_congestion_window_;
		num_acked_packets_ = 0;
    }else{
        congestion_window_ = congestion_window_*RenoBeta();
        if(congestion_window_<min_congestion_window_){
            congestion_window_=min_congestion_window_;
        }
		num_acked_packets_ = 0;
    }
    last_drop_=event_time;
}
void LpTcpSender::OnPacketSent(
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
  hybrid_slow_start_.OnPacketSent(packet_number);
}

bool LpTcpSender::CanSend(QuicByteCount bytes_in_flight) {
  if (GetCongestionWindow() > bytes_in_flight) {
    return true;
  }
  if (min4_mode_ && bytes_in_flight < 4 * kDefaultTCPMSS) {
    return true;
  }
  return false;
}

QuicBandwidth LpTcpSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (InRecovery() ? 1 : 1.25));
}

QuicBandwidth LpTcpSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool LpTcpSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool LpTcpSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool LpTcpSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool LpTcpSender::ShouldSendProbingPacket() const {
  return false;
}

void LpTcpSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

std::string LpTcpSender::GetDebugState() const {
  return "";
}

void LpTcpSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void LpTcpSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void LpTcpSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void LpTcpSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void LpTcpSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void LpTcpSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}
void LpTcpSender::OnPacketLost(QuicPacketNumber packet_number,
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
  congestion_window_ = congestion_window_*RenoBeta();
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  num_acked_packets_ = 0;
  slowstart_threshold_ = congestion_window_;
}
void LpTcpSender::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time){
  if (congestion_window_ >= max_congestion_window_) {
    return;
  }
  if (InSlowStart()) {
    // TCP slow start, exponential growth, increase by one for each ACK.
    congestion_window_ += kDefaultTCPMSS;
    DLOG(INFO) << "Slow start; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
    return;
  }
   ++num_acked_packets_;
   // Divide by num_connections to smoothly increase the CWND at a faster rate
   // than conventional Reno.
   if (num_acked_packets_ * num_connections_ >=
       congestion_window_ / kDefaultTCPMSS) {
     congestion_window_ += kDefaultTCPMSS;
     num_acked_packets_ = 0;
   }
}
QuicByteCount LpTcpSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount LpTcpSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}
void LpTcpSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}
void LpTcpSender::OnConnectionMigration() {
  hybrid_slow_start_.Restart();
  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType LpTcpSender::GetCongestionControlType() const {
  return kLpTcp;
}
void LpTcpSender::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
}
}
