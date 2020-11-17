#include "cubic_plus_sender_bytes.h"
#include "unacked_packet_map.h"
#include "rtt_stats.h"
#include "flag_impl.h"
#include "flag_util_impl.h"
#include "logging.h"
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <string>
#include "random.h"
namespace dqc{

namespace {
// Maximum window to allow when doing bandwidth resumption.
const QuicPacketCount kMaxResumptionCongestionWindow = 200;
// Constants based on TCP defaults.
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const float kRenoBeta = 0.7f;  // Reno backoff factor.
// The time after which the current min_rtt value expires.
const TimeDelta kMinRttBaseDuration=TimeDelta::FromMilliseconds(2500);
const TimeDelta kMinRttRandDuration=TimeDelta::FromMilliseconds(500);
const TimeDelta kMinRttExpiry = TimeDelta::FromMilliseconds(3000);
// The minimum time the connection can spend in PROBE_RTT mode.
const TimeDelta kProbeRttTime = TimeDelta::FromMilliseconds(200);
const QuicByteCount kDefaultMinimumCongestionWindow =4* kDefaultTCPMSS ;//2 * kDefaultTCPMSS;
const float kDerivedHighCWNDGain = 2.0f;
const QuicRoundTripCount kBandwidthWindowSize=10;
const float kStartupGrowthTarget = 1.25;
const float kStartupAfterLossGain = 1.5f;
const QuicRoundTripCount kRoundTripsWithoutGrowthBeforeExitingStartup = 3;
const float kModerateProbeRttMultiplier = 0.5;
const float kSimilarMinRttThreshold = 1.125;
const size_t kGainCycleLength=8;
}

CubicPlusSender::CubicPlusSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
	const UnackedPacketMap* unacked_packets,
    bool reno,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats,
	Random* random)
    : rtt_stats_(rtt_stats),
      stats_(stats),
	  unacked_packets_(unacked_packets),
	  random_(random),
	  mode_(STARTUP),
	  round_trip_count_(0),
	  max_bandwidth_(kBandwidthWindowSize, QuicBandwidth::Zero(), 0),
      max_ack_height_(kBandwidthWindowSize, 0, 0),
      max_window_height_(kBandwidthWindowSize, 0, 0),
      aggregation_epoch_start_time_(ProtoTime::Zero()),
      aggregation_epoch_bytes_(0),
      enable_ack_aggregation_during_startup_(false),
      expire_ack_aggregation_in_startup_(false),
	  probe_rtt_based_on_bdp_(true),
      slower_startup_(false),
      rate_based_startup_(false),
      startup_rate_reduction_multiplier_(0),
      startup_bytes_lost_(0),
	  min_rtt_(TimeDelta::Zero()),
      min_rtt_timestamp_(ProtoTime::Zero()),
      high_gain_(kDerivedHighCWNDGain/*kDefaultHighGain*/),
      high_cwnd_gain_(kDerivedHighCWNDGain/*kDefaultHighGain*/),
      drain_gain_(1.f /kDerivedHighCWNDGain/*kDefaultHighGain*/),
	  pacing_rate_(QuicBandwidth::Zero()),
	  num_startup_rtts_(kRoundTripsWithoutGrowthBeforeExitingStartup),
      exit_startup_on_loss_(false),
      cycle_current_offset_(0),
      last_cycle_start_(ProtoTime::Zero()),
      is_at_full_bandwidth_(false),
      rounds_without_bandwidth_gain_(0),
      bandwidth_at_last_round_(QuicBandwidth::Zero()),
      exiting_quiescence_(false),
      exit_probe_rtt_at_(ProtoTime::Zero()),
      probe_rtt_round_passed_(false),
      last_sample_is_app_limited_(false),
      has_non_app_limited_sample_(false),
      flexible_app_limited_(false),
	  reno_(reno),
      num_connections_(kDefaultNumConnections),
      min4_mode_(false),
      last_cutback_exited_slowstart_(false),
      slow_start_large_reduction_(false),
      no_prr_(false),
      cubic_(clock),
      num_acked_packets_(0),
      congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
	  initial_congestion_window_(initial_tcp_congestion_window *
	                                   kDefaultTCPMSS),
	  min_congestion_window_(kDefaultMinimumCongestionWindow),
      max_congestion_window_(max_congestion_window * kDefaultTCPMSS),
      slowstart_threshold_(max_congestion_window * kDefaultTCPMSS),
      initial_tcp_congestion_window_(initial_tcp_congestion_window *
                                     kDefaultTCPMSS),
      initial_max_tcp_congestion_window_(max_congestion_window *
                                         kDefaultTCPMSS),
      min_slow_start_exit_window_(min_congestion_window_),
      probe_rtt_skipped_if_similar_rtt_(false),
      probe_rtt_disabled_if_app_limited_(false),
      app_limited_since_last_probe_rtt_(false),
      min_rtt_since_last_probe_rtt_(TimeDelta::Infinite()),
      always_get_bw_sample_when_acked_(
          GetQuicReloadableFlag(quic_always_get_bw_sample_when_acked)){
        min_rtt_expiration_=kMinRttBaseDuration+
        TimeDelta::FromMilliseconds(random_->nextInt(0,kMinRttRandDuration.ToMilliseconds()));
		EnterStartupMode(clock->Now());
}

CubicPlusSender::~CubicPlusSender() {}

/*void CubicPlusSender::SetFromConfig(const QuicConfig& config,
                                        Perspective perspective) {
  if (perspective == Perspective::IS_SERVER) {
    if (!GetQuicReloadableFlag(quic_unified_iw_options)) {
      if (config.HasReceivedConnectionOptions() &&
          ContainsQuicTag(config.ReceivedConnectionOptions(), kIW03)) {
        // Initial window experiment.
        SetInitialCongestionWindowInPackets(3);
      }
      if (config.HasReceivedConnectionOptions() &&
          ContainsQuicTag(config.ReceivedConnectionOptions(), kIW10)) {
        // Initial window experiment.
        SetInitialCongestionWindowInPackets(10);
      }
      if (config.HasReceivedConnectionOptions() &&
          ContainsQuicTag(config.ReceivedConnectionOptions(), kIW20)) {
        // Initial window experiment.
        SetInitialCongestionWindowInPackets(20);
      }
      if (config.HasReceivedConnectionOptions() &&
          ContainsQuicTag(config.ReceivedConnectionOptions(), kIW50)) {
        // Initial window experiment.
        SetInitialCongestionWindowInPackets(50);
      }
      if (config.HasReceivedConnectionOptions() &&
          ContainsQuicTag(config.ReceivedConnectionOptions(), kMIN1)) {
        // Min CWND experiment.
        SetMinCongestionWindowInPackets(1);
      }
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kMIN4)) {
      // Min CWND of 4 experiment.
      min4_mode_ = true;
      SetMinCongestionWindowInPackets(1);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kSSLR)) {
      // Slow Start Fast Exit experiment.
      slow_start_large_reduction_ = true;
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kNPRR)) {
      // Use unity pacing instead of PRR.
      no_prr_ = true;
    }
  }
}
*/
void CubicPlusSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}

float CubicPlusSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}

void CubicPlusSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {

	const QuicByteCount total_bytes_acked_before = sampler_.total_bytes_acked();
	  bool is_round_start = false;
	  bool min_rtt_expired = false;
	  DiscardLostPackets(lost_packets);

	  QuicByteCount excess_acked = 0;
	  if (!acked_packets.empty()) {
	    QuicPacketNumber last_acked_packet = acked_packets.rbegin()->packet_number;
	    is_round_start = UpdateRoundTripCounter(last_acked_packet);
	    min_rtt_expired = UpdateBandwidthAndMinRtt(event_time, acked_packets);
	    //UpdateRecoveryState(last_acked_packet, !lost_packets.empty(),
	    //                    is_round_start);

	    const QuicByteCount bytes_acked =
	        sampler_.total_bytes_acked() - total_bytes_acked_before;

	    excess_acked = UpdateAckAggregationBytes(event_time, bytes_acked);
	  }
	  if(mode_==PROBE_BW){
		for (const LostPacket& lost_packet : lost_packets) {
		   OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
		                prior_in_flight);
		 }
		 for (const AckedPacket acked_packet : acked_packets) {
		   OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
		                 prior_in_flight, event_time);
		 }
         if(mode_==PROBE_BW){
            UpdateGainCyclePhase(event_time, prior_in_flight, !lost_packets.empty());
            max_window_height_.Update(congestion_window_,round_trip_count_);             
         }
	  }
	  // Handle logic specific to STARTUP and DRAIN modes.
	  if (is_round_start && !is_at_full_bandwidth_) {
	    CheckIfFullBandwidthReached();
	  }
	  MaybeExitStartupOrDrain(event_time);

	  // Handle logic specific to PROBE_RTT.
	  MaybeEnterOrExitProbeRtt(event_time, is_round_start, min_rtt_expired);

	  // Calculate number of packets acked and lost.
	  QuicByteCount bytes_acked =
	      sampler_.total_bytes_acked() - total_bytes_acked_before;
	  //QuicByteCount bytes_lost = 0;
	  /*for (const auto& packet : lost_packets) {
	    bytes_lost += packet.bytes_lost;
	  }*/
	  CalculatePacingRate();
	  if(mode_==STARTUP||mode_==DRAIN||mode_==PROBE_RTT){
		  CalculateCongestionWindow(bytes_acked, excess_acked);
	  }
	  
	  sampler_.RemoveObsoletePackets(unacked_packets_->GetLeastUnacked());
}

void CubicPlusSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
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
}

void CubicPlusSender::OnPacketSent(
    ProtoTime sent_time,
    QuicByteCount bytes_in_flight,
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
  if (!aggregation_epoch_start_time_.IsInitialized()) {
    aggregation_epoch_start_time_ = sent_time;
  }
  //last_sent_packet_ = packet_number;

  if (bytes_in_flight == 0 && sampler_.is_app_limited()) {
    exiting_quiescence_ = true;
  }
  sampler_.OnPacketSent(sent_time, packet_number, bytes, bytes_in_flight,
                        is_retransmittable);
}

bool CubicPlusSender::CanSend(QuicByteCount bytes_in_flight) {
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

QuicBandwidth CubicPlusSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
	  if (pacing_rate_.IsZero()) {
	    return high_gain_ * QuicBandwidth::FromBytesAndTimeDelta(
	                            initial_congestion_window_, GetMinRtt());
	  }
	  return pacing_rate_;
}
QuicBandwidth CubicPlusSender::BandwidthEstimate() const{
QuicBandwidth bw=QuicBandwidth::Zero();
  if(mode_==PROBE_BW){
	//TimeDelta srtt = rtt_stats_->smoothed_rtt();
	//bw=QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
	bw=BandwidthEstimateFromSample();
}else{
	bw=BandwidthEstimateFromSample();
}
  return  bw;
}
QuicBandwidth CubicPlusSender::BandwidthEstimateFromSample() const {
  return max_bandwidth_.GetBest();
}

bool CubicPlusSender::InSlowStart() const {
	return mode_ == STARTUP;
}

bool CubicPlusSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool CubicPlusSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool CubicPlusSender::ShouldSendProbingPacket() const {
  return false;
}

void CubicPlusSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  HandleRetransmissionTimeout();
}

std::string CubicPlusSender::GetDebugState() const {
  return "";
}

void CubicPlusSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}
void CubicPlusSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void CubicPlusSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void CubicPlusSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void CubicPlusSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
  cubic_.SetNumConnections(num_connections_);
}

void CubicPlusSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void CubicPlusSender::OnPacketLost(QuicPacketNumber packet_number,
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
  } else if (reno_) {
    //congestion_window_ = congestion_window_ * RenoBeta();
	  congestion_window_ = GetTargetCongestionWindow(1.0) * RenoBeta();
  } else {
    //congestion_window_ =cubic_.CongestionWindowAfterPacketLoss(congestion_window_);
	congestion_window_ =cubic_.CongestionWindowAfterPacketLoss(GetTargetCongestionWindow(1.0));
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  // Reset packet count from congestion avoidance mode. We start counting again
  // when we're out of recovery.
  num_acked_packets_ = 0;
  DLOG(INFO)<< "Incoming loss; congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_;
}
QuicByteCount CubicPlusSender::GetCongestionWindow() const {
	  if (mode_ == PROBE_RTT) {
        QuicByteCount cwnd=ProbeRttCongestionWindow()>congestion_window_?congestion_window_:ProbeRttCongestionWindow();
	    return cwnd;
	  }
	return congestion_window_;
}

QuicByteCount CubicPlusSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void CubicPlusSender::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time) {
  //QUIC_BUG_IF(InRecovery()) << "Never increase the CWND during recovery.";
  // Do not increase the congestion window unless the sender is close to using
  // the current window.
  if (!IsCwndLimited(prior_in_flight)) {
    cubic_.OnApplicationLimited();
    return;
  }
  if (congestion_window_ >= max_congestion_window_) {
    return;
  }
  /*if (InSlowStart()) {
    congestion_window_ += kDefaultTCPMSS;
    return;
  }*/
  // Congestion avoidance.
  if (reno_) {
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
  } else {
    congestion_window_ = std::min(
        max_congestion_window_,
        cubic_.CongestionWindowAfterAck(acked_bytes, congestion_window_,
                                        rtt_stats_->min_rtt(), event_time));
    /*QUIC_DVLOG(1)*/DLOG(INFO) << "Cubic; congestion window: " << congestion_window_
                  << " slowstart threshold: " << slowstart_threshold_;
  }
}

void CubicPlusSender::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void CubicPlusSender::OnConnectionMigration() {
  prr_ = PrrSender();
  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType CubicPlusSender::GetCongestionControlType() const {
  return reno_ ? kRenoPlus : kCubicPlus;
}
void CubicPlusSender::DiscardLostPackets(const LostPacketVector& lost_packets) {
  for (const LostPacket& packet : lost_packets) {
    sampler_.OnPacketLost(packet.packet_number);
    if (mode_ == STARTUP) {
      if (startup_rate_reduction_multiplier_ != 0) {
        startup_bytes_lost_ += packet.bytes_lost;
      }
    }
  }
}
bool CubicPlusSender::UpdateRoundTripCounter(QuicPacketNumber last_acked_packet) {
  if (!current_round_trip_end_.IsInitialized()||
      last_acked_packet > current_round_trip_end_) {
    round_trip_count_++;
    current_round_trip_end_ = largest_sent_packet_number_;//last_sent_packet_;
    return true;
  }
  return false;
}
bool CubicPlusSender::UpdateBandwidthAndMinRtt(
    ProtoTime now,
    const AckedPacketVector& acked_packets) {
  TimeDelta sample_min_rtt = TimeDelta::Infinite();
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

    last_sample_is_app_limited_ = bandwidth_sample.state_at_send.is_app_limited;
    has_non_app_limited_sample_ |=
        !bandwidth_sample.state_at_send.is_app_limited;
    if (!bandwidth_sample.rtt.IsZero()) {
      sample_min_rtt = std::min(sample_min_rtt, bandwidth_sample.rtt);
    }

    if (!bandwidth_sample.state_at_send.is_app_limited ||
        bandwidth_sample.bandwidth > BandwidthEstimateFromSample()) {
      max_bandwidth_.Update(bandwidth_sample.bandwidth, round_trip_count_);
    }
  }

  // If none of the RTT samples are valid, return immediately.
  if (sample_min_rtt.IsInfinite()) {
    return false;
  }
  min_rtt_since_last_probe_rtt_ =
      std::min(min_rtt_since_last_probe_rtt_, sample_min_rtt);

  // Do not expire min_rtt if none was ever available.
  bool min_rtt_expired =
      !min_rtt_.IsZero() && (now > (min_rtt_timestamp_ +min_rtt_expiration_));//kMinRttExpiry

  if (min_rtt_expired || sample_min_rtt < min_rtt_ || min_rtt_.IsZero()) {
    if (min_rtt_expired && ShouldExtendMinRttExpiry()) {
      min_rtt_expired = false;
    } else {
      min_rtt_ = sample_min_rtt;
    }
    min_rtt_timestamp_ = now;
    // Reset since_last_probe_rtt fields.
    min_rtt_since_last_probe_rtt_ = TimeDelta::Infinite();
    app_limited_since_last_probe_rtt_ = false;
  }
  DCHECK(!min_rtt_.IsZero());

  return min_rtt_expired;
}
bool CubicPlusSender::ShouldExtendMinRttExpiry() const {
  if (probe_rtt_disabled_if_app_limited_ && app_limited_since_last_probe_rtt_) {
    // Extend the current min_rtt if we've been app limited recently.
    return true;
  }
  const bool min_rtt_increased_since_last_probe =
      min_rtt_since_last_probe_rtt_ > min_rtt_ * kSimilarMinRttThreshold;
  if (probe_rtt_skipped_if_similar_rtt_ && app_limited_since_last_probe_rtt_ &&
      !min_rtt_increased_since_last_probe) {
    // Extend the current min_rtt if we've been app limited recently and an rtt
    // has been measured in that time that's less than 12.5% more than the
    // current min_rtt.
    return true;
  }
  return false;
}
void CubicPlusSender::UpdateGainCyclePhase(ProtoTime now,
                                     QuicByteCount prior_in_flight,
                                     bool has_losses){
	pacing_gain_=1.25;
}
void CubicPlusSender::CheckIfFullBandwidthReached() {
  if (last_sample_is_app_limited_) {
    return;
  }

  QuicBandwidth target = bandwidth_at_last_round_ * kStartupGrowthTarget;
  if (BandwidthEstimateFromSample() >= target) {
    bandwidth_at_last_round_ = BandwidthEstimateFromSample();
    rounds_without_bandwidth_gain_ = 0;
    if (expire_ack_aggregation_in_startup_) {
      // Expire old excess delivery measurements now that bandwidth increased.
      max_ack_height_.Reset(0, round_trip_count_);
    }
    return;
  }

  rounds_without_bandwidth_gain_++;
  if ((rounds_without_bandwidth_gain_ >= num_startup_rtts_) ||
      (exit_startup_on_loss_ /*&& InRecovery()*/)) {
    DCHECK(has_non_app_limited_sample_);
    is_at_full_bandwidth_ = true;
  }
}

void CubicPlusSender::MaybeExitStartupOrDrain(ProtoTime now) {
  if (mode_ == STARTUP && is_at_full_bandwidth_) {
    mode_ = DRAIN;
    pacing_gain_ = drain_gain_;
    congestion_window_gain_ = high_cwnd_gain_;
  }
  if (mode_ == DRAIN &&
      unacked_packets_->bytes_in_flight() <= GetTargetCongestionWindow(1)) {
    EnterProbeBandwidthMode(now);
  }
}
void CubicPlusSender::MaybeEnterOrExitProbeRtt(ProtoTime now,
                                         bool is_round_start,
                                         bool min_rtt_expired) {
  if (min_rtt_expired && !exiting_quiescence_ && mode_ != PROBE_RTT) {
    mode_ = PROBE_RTT;
    pacing_gain_ = 1;
    exit_probe_rtt_at_ = ProtoTime::Zero();
    congestion_window_before_probe_rtt_=congestion_window_;
    max_window_height_.Reset(congestion_window_,round_trip_count_);
    min_rtt_expiration_=kMinRttBaseDuration+
    TimeDelta::FromMilliseconds(random_->nextInt(0,kMinRttRandDuration.ToMilliseconds()));
  }

  if (mode_ == PROBE_RTT) {
    sampler_.OnAppLimited();

    if (exit_probe_rtt_at_ == ProtoTime::Zero()) {
      // If the window has reached the appropriate size, schedule exiting
      // PROBE_RTT.  The CWND during PROBE_RTT is kMinimumCongestionWindow, but
      // we allow an extra packet since QUIC checks CWND before sending a
      // packet.
      if (unacked_packets_->bytes_in_flight() <
          ProbeRttCongestionWindow() + kMaxOutgoingPacketSize) {
        exit_probe_rtt_at_ = now + kProbeRttTime;
        probe_rtt_round_passed_ = false;
      }
    } else {
      if (is_round_start) {
        probe_rtt_round_passed_ = true;
      }
      if (now >= exit_probe_rtt_at_ && probe_rtt_round_passed_) {
        min_rtt_timestamp_ = now;
        if (!is_at_full_bandwidth_) {
          EnterStartupMode(now);
        } else {
          SwitchProbeRttToProbeBandwidth(now);
        }
      }
    }
  }

  exiting_quiescence_ = false;
}
TimeDelta CubicPlusSender::GetMinRtt() const {
  return !min_rtt_.IsZero() ? min_rtt_ : rtt_stats_->initial_rtt();
}

QuicByteCount CubicPlusSender::GetTargetCongestionWindow(float gain) const {
  QuicByteCount bdp = GetMinRtt() * BandwidthEstimateFromSample();
  QuicByteCount congestion_window = gain * bdp;

  // BDP estimate will be zero if no bandwidth samples are available yet.
  if (congestion_window == 0) {
    congestion_window = gain * initial_congestion_window_;
  }

  return std::max(congestion_window, min_congestion_window_);
}
QuicByteCount CubicPlusSender::ProbeRttCongestionWindow() const {
  if (probe_rtt_based_on_bdp_) {
    return GetTargetCongestionWindow(kModerateProbeRttMultiplier);
  }
  return min_congestion_window_;
}
void CubicPlusSender::EnterStartupMode(ProtoTime now) {
  mode_ = STARTUP;
  pacing_gain_ = high_gain_;
  congestion_window_gain_ = high_cwnd_gain_;
}

void CubicPlusSender::EnterProbeBandwidthMode(ProtoTime now) {
	mode_ = PROBE_BW;
	congestion_window_gain_=1.0;
	//add by zsy, enter aimd mode
	congestion_window_=GetTargetCongestionWindow(congestion_window_gain_);
    max_window_height_.Update(congestion_window_,round_trip_count_);
	last_cycle_start_ = now;
	pacing_gain_=1.25;
}
void CubicPlusSender::SwitchProbeRttToProbeBandwidth(ProtoTime now){
	//EnterProbeBandwidthMode(now);
	mode_ = PROBE_BW;
    congestion_window_gain_=1.0;
    max_window_height_.Update(congestion_window_,round_trip_count_);
	last_cycle_start_ = now;
	pacing_gain_=1.25;
    if(reno_){
        
    }else{
        //ResetCubicCongestionWindow(congestion_window_,congestion_window_before_probe_rtt_);
    }
    
}
void CubicPlusSender::CalculateCongestionWindow(QuicByteCount bytes_acked,QuicByteCount excess_acked){
	  if (mode_ == PROBE_RTT) {
	    return;
	  }

	  QuicByteCount target_window =
	      GetTargetCongestionWindow(congestion_window_gain_);
	  if (is_at_full_bandwidth_) {
	    // Add the max recently measured ack aggregation to CWND.
	    target_window += max_ack_height_.GetBest();
	  } else if (enable_ack_aggregation_during_startup_) {
	    // Add the most recent excess acked.  Because CWND never decreases in
	    // STARTUP, this will automatically create a very localized max filter.
	    target_window += excess_acked;
	  }

	  // Instead of immediately setting the target CWND as the new one, BBR grows
	  // the CWND towards |target_window| by only increasing it |bytes_acked| at a
	  // time.
	  const bool add_bytes_acked =
	      !GetQuicReloadableFlag(quic_bbr_no_bytes_acked_in_startup_recovery) /*||
	      !InRecovery()*/;
	  if (is_at_full_bandwidth_) {
	    congestion_window_ =
	        std::min(target_window, congestion_window_ + bytes_acked);
	  } else if (add_bytes_acked &&
	             (congestion_window_ < target_window ||
	              sampler_.total_bytes_acked() < initial_congestion_window_)) {
	    // If the connection is not yet out of startup phase, do not decrease the
	    // window.
	    congestion_window_ = congestion_window_ + bytes_acked;
	  }

	  // Enforce the limits on the congestion window.
	  congestion_window_ = std::max(congestion_window_, min_congestion_window_);
	  congestion_window_ = std::min(congestion_window_, max_congestion_window_);
}
QuicByteCount CubicPlusSender::UpdateAckAggregationBytes(
    ProtoTime ack_time,
    QuicByteCount newly_acked_bytes) {
  // Compute how many bytes are expected to be delivered, assuming max bandwidth
  // is correct.
  QuicByteCount expected_bytes_acked =
      max_bandwidth_.GetBest() * (ack_time - aggregation_epoch_start_time_);
  // Reset the current aggregation epoch as soon as the ack arrival rate is less
  // than or equal to the max bandwidth.
  if (aggregation_epoch_bytes_ <= expected_bytes_acked) {
    // Reset to start measuring a new aggregation epoch.
    aggregation_epoch_bytes_ = newly_acked_bytes;
    aggregation_epoch_start_time_ = ack_time;
    return 0;
  }

  // Compute how many extra bytes were delivered vs max bandwidth.
  // Include the bytes most recently acknowledged to account for stretch acks.
  aggregation_epoch_bytes_ += newly_acked_bytes;
  max_ack_height_.Update(aggregation_epoch_bytes_ - expected_bytes_acked,
                         round_trip_count_);
  return aggregation_epoch_bytes_ - expected_bytes_acked;
}
void CubicPlusSender::CalculatePacingRate(){
	// We pace at twice the rate of the underlying sender's bandwidth estimate
	// during slow start and 1.25x during congestion avoidance to ensure pacing
	// doesn't prevent us from filling the window.
	if (BandwidthEstimateFromSample().IsZero()) {
	   return;
	}
	if(mode_==PROBE_BW){
		const QuicBandwidth bandwidth =BandwidthEstimateFromSample();
		pacing_rate_=bandwidth * ((no_prr_ && InRecovery() ? 1 : 1.25));
	}else{
		  QuicBandwidth target_rate = pacing_gain_ * BandwidthEstimateFromSample();
		  if (is_at_full_bandwidth_) {
		    pacing_rate_ = target_rate;
		    return;
		  }

		  // Pace at the rate of initial_window / RTT as soon as RTT measurements are
		  // available.
		  if (pacing_rate_.IsZero() && !rtt_stats_->min_rtt().IsZero()) {
		    pacing_rate_ = QuicBandwidth::FromBytesAndTimeDelta(
		        initial_congestion_window_, rtt_stats_->min_rtt());
		    return;
		  }
		  // Slow the pacing rate in STARTUP once loss has ever been detected.
		  const bool has_ever_detected_loss = false;//end_recovery_at_.IsInitialized();
		  if (slower_startup_ && has_ever_detected_loss &&
		      has_non_app_limited_sample_) {
		    pacing_rate_ = kStartupAfterLossGain * BandwidthEstimateFromSample();
		    return;
		  }

		  // Slow the pacing rate in STARTUP by the bytes_lost / CWND.
		  if (startup_rate_reduction_multiplier_ != 0 && has_ever_detected_loss &&
		      has_non_app_limited_sample_) {
		    pacing_rate_ =
		        (1 - (startup_bytes_lost_ * startup_rate_reduction_multiplier_ * 1.0f /
		              congestion_window_)) *
		        target_rate;
		    // Ensure the pacing rate doesn't drop below the startup growth target times
		    // the bandwidth estimate.
		    pacing_rate_ =
		        std::max(pacing_rate_, kStartupGrowthTarget * BandwidthEstimateFromSample());
		    return;
		  }

		  // Do not decrease the pacing rate during startup.
		  pacing_rate_ = std::max(pacing_rate_, target_rate);
	}
}
void CubicPlusSender::ResetCubicCongestionWindow(QuicByteCount start,QuicByteCount target){
	QuicByteCount max=
			start>target?start:target;
	cubic_.UpdateCongestionWindowEstimate(start,max);
}
QuicByteCount CubicPlusSender::GetBestCongestionWindow() const{
    return max_ack_height_.GetBest();
}
}
