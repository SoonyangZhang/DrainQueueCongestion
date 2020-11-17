#include "proto_dctcp_sender.h"
#include "unacked_packet_map.h"
#include "rtt_stats.h"
#include "logging.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <iostream>
#include "flag_impl.h"
namespace dqc{

namespace {
const QuicPacketCount kMaxResumptionCongestionWindow = 200;
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
const uint32_t dctcp_shift_g=4;
#define DCTCP_MAX_ALPHA	1024U
template<class T> 
const T& min_not_zero(const T& x, const T& y)
{
   T __x = (x);
   T __y = (y);
   return __x == 0 ? __y : ((__y == 0) ? __x : std::min(__x, __y));
}
}  // namespace

ProtoDctcpSender::ProtoDctcpSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
   const UnackedPacketMap* unacked_packets,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    :rtt_stats_(rtt_stats),
    unacked_packets_(unacked_packets),
    stats_(stats),
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
    reno_beta_(GetQuicReloadableFlag(reno_beta)){
        alpha_=DCTCP_MAX_ALPHA;
    }

ProtoDctcpSender::~ProtoDctcpSender() {}
void ProtoDctcpSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
}
float ProtoDctcpSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + reno_beta_) / num_connections_;
}
void ProtoDctcpSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
    if (rtt_updated && InSlowStart() &&
        hybrid_slow_start_.ShouldExitSlowStart(
        rtt_stats_->latest_rtt(), rtt_stats_->min_rtt(),
        GetCongestionWindow() / kDefaultTCPMSS)){
        ExitSlowstart();
  }

    QuicPacketNumber last_acked_packet = acked_packets.rbegin()->packet_number;
    UpdateRoundTripAlpha(last_acked_packet);
  
  for (const LostPacket& lost_packet : lost_packets) {
    OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                 prior_in_flight);
  }
  if(enc_ece_rcvd_){
      OnReduceCongstionWindow(last_acked_packet,prior_in_flight);
      enc_ece_rcvd_=false;
  }
  for (const AckedPacket acked_packet : acked_packets) {
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                  prior_in_flight, event_time);
  }
}

void ProtoDctcpSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
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

void ProtoDctcpSender::OnPacketSent(
    ProtoTime /*sent_time*/,
    QuicByteCount /*bytes_in_flight*/,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    HasRetransmittableData is_retransmittable) {
  if (InSlowStart()) {
    ++(stats_->slowstart_packets_sent);
  }
  last_sent_packet_ = packet_number;
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

bool ProtoDctcpSender::CanSend(QuicByteCount bytes_in_flight) {
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

QuicBandwidth ProtoDctcpSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (no_prr_ && InRecovery() ? 1 : 1.25));
}

QuicBandwidth ProtoDctcpSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool ProtoDctcpSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool ProtoDctcpSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool ProtoDctcpSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool ProtoDctcpSender::ShouldSendProbingPacket() const {
  return false;
}

void ProtoDctcpSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

std::string ProtoDctcpSender::GetDebugState() const {
  return "";
}

void ProtoDctcpSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}
void ProtoDctcpSender::OnUpdateEcnBytes(uint64_t ecn_ce_count){
    QuicByteCount temp=ecn_ce_count_;
    ecn_ce_count_=ecn_ce_count;
    if(temp!=ecn_ce_count_){
        enc_ece_rcvd_=true;
    }
}
void ProtoDctcpSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void ProtoDctcpSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void ProtoDctcpSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void ProtoDctcpSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void ProtoDctcpSender::OnPacketLost(QuicPacketNumber packet_number,
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

  // TODO(b/77268641): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else{
        congestion_window_ = congestion_window_ * RenoBeta();
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  num_acked_packets_ = 0;
  DLOG(INFO)<< "Incoming loss; congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_;
}
void ProtoDctcpSender::OnReduceCongstionWindow(QuicPacketNumber packet_number,
                                QuicByteCount prior_in_flight){
  QuicByteCount lost_bytes=0;
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
    //DCTCP Method
    QuicByteCount temp=congestion_window_-((congestion_window_*alpha_)>>11U);
    congestion_window_ =temp;
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  num_acked_packets_ = 0;    
}
QuicByteCount ProtoDctcpSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount ProtoDctcpSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void ProtoDctcpSender::MaybeIncreaseCwnd(
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
  // Congestion avoidance.
  if (true) {
    // Classic Reno congestion avoidance.
    ++num_acked_packets_;
    // Divide by num_connections to smoothly increase the CWND at a faster rate
    // than conventional Reno.
    if (num_acked_packets_ * num_connections_ >=
        congestion_window_ / kDefaultTCPMSS) {
      congestion_window_ += kDefaultTCPMSS;
      num_acked_packets_ = 0;
    }

    /*QUIC_DVLOG(1) */DLOG(INFO)<< "Reno; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_
                  << " congestion window count: " << num_acked_packets_;
  }
}

void ProtoDctcpSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}
void ProtoDctcpSender::UpdateRoundTripAlpha(QuicPacketNumber last_acked_packet){
  if (!current_round_trip_end_.IsInitialized()||
      last_acked_packet > current_round_trip_end_) {
    uint32_t delivered_ce=uint32_t(ecn_ce_count_-old_ce_count_);
    uint32_t alpha=alpha_;
    alpha -= min_not_zero(alpha, alpha >> dctcp_shift_g);
    QuicByteCount total_bytes_sent=unacked_packets_->delivered();
    uint32_t delivered=uint32_t(total_bytes_sent-old_bytes_sent_);
    if(delivered_ce){
        delivered_ce <<= (10 - dctcp_shift_g);
        delivered_ce /= std::max(1U, delivered);
        alpha = std::min(alpha + delivered_ce, DCTCP_MAX_ALPHA);
        
    }
    alpha_=alpha;
    old_bytes_sent_=total_bytes_sent;
    old_ce_count_=ecn_ce_count_;
    current_round_trip_end_ = last_sent_packet_;
  }
}
void ProtoDctcpSender::OnConnectionMigration() {
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

CongestionControlType ProtoDctcpSender::GetCongestionControlType() const {
  return kDctcp;
}
}
