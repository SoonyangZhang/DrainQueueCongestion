// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic_bbr_sender.h"
#include <algorithm>
#include <sstream>
#include <string>

#include "unacked_packet_map.h"
#include "flag_impl.h"
#include "flag_util_impl.h"
#include "rtt_stats.h"
#include "random.h"
#include "proto_constants.h"
#include "logging.h"
#include "quic_logging.h"
#include <ostream>

namespace dqc {

namespace {
// Constants based on TCP defaults.
// The minimum CWND to ensure delayed acks don't reduce bandwidth measurements.
// Does not inflate the pacing rate.
const QuicByteCount kDefaultMinimumCongestionWindow = 4 * kMaxSegmentSize;

// The gain used for the STARTUP, equal to 2/ln(2).
const float kDefaultHighGain = 2.885f;
// The newly derived gain for STARTUP, equal to 4 * ln(2)
const float kDerivedHighGain = 2.773f;
// The newly derived CWND gain for STARTUP, 2.
const float kDerivedHighCWNDGain = 2.0f;
// The cycle of gains used during the PROBE_BW stage.
const float kPacingGain[] = {1.25, 0.75, 1, 1, 1, 1, 1, 1};

// The length of the gain cycle.
const size_t kGainCycleLength = sizeof(kPacingGain) / sizeof(kPacingGain[0]);
// The size of the bandwidth filter window, in round-trips.
const QuicRoundTripCount kBandwidthWindowSize = kGainCycleLength + 2;

// The time after which the current min_rtt value expires.
const TimeDelta kMinRttExpiry = TimeDelta::FromSeconds(10);
// The minimum time the connection can spend in PROBE_RTT mode.
const TimeDelta kProbeRttTime = TimeDelta::FromMilliseconds(200);
// If the bandwidth does not increase by the factor of |kStartupGrowthTarget|
// within |kRoundTripsWithoutGrowthBeforeExitingStartup| rounds, the connection
// will exit the STARTUP mode.
const float kStartupGrowthTarget = 1.25;
const QuicRoundTripCount kRoundTripsWithoutGrowthBeforeExitingStartup = 3;
}  // namespace

QuicBbrSender::DebugState::DebugState(const QuicBbrSender& sender)
    : mode(sender.mode_),
      max_bandwidth(sender.max_bandwidth_.GetBest()),
      round_trip_count(sender.round_trip_count_),
      gain_cycle_index(sender.cycle_current_offset_),
      congestion_window(sender.congestion_window_),
      is_at_full_bandwidth(sender.is_at_full_bandwidth_),
      bandwidth_at_last_round(sender.bandwidth_at_last_round_),
      rounds_without_bandwidth_gain(sender.rounds_without_bandwidth_gain_),
      min_rtt(sender.min_rtt_),
      min_rtt_timestamp(sender.min_rtt_timestamp_),
      recovery_state(sender.recovery_state_),
      recovery_window(sender.recovery_window_),
      last_sample_is_app_limited(sender.last_sample_is_app_limited_),
      end_of_app_limited_phase(sender.sampler_.end_of_app_limited_phase()) {}

QuicBbrSender::DebugState::DebugState(const DebugState& state) = default;

QuicBbrSender::QuicBbrSender(ProtoTime now,
                     const RttStats* rtt_stats,
                     const UnackedPacketMap* unacked_packets,
                     QuicPacketCount initial_tcp_congestion_window,
                     QuicPacketCount max_tcp_congestion_window,
                     Random* random,QuicConnectionStats* stats,bool drain_to_target)
    : rtt_stats_(rtt_stats),
      unacked_packets_(unacked_packets),
      random_(random),
      stats_(stats),
      mode_(STARTUP),
      sampler_(unacked_packets, kBandwidthWindowSize),
      round_trip_count_(0),
      num_loss_events_in_round_(0),
      bytes_lost_in_round_(0),
      max_bandwidth_(kBandwidthWindowSize, QuicBandwidth::Zero(), 0),
      min_rtt_(TimeDelta::Zero()),
      min_rtt_timestamp_(ProtoTime::Zero()),
      congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
      initial_congestion_window_(initial_tcp_congestion_window *
                                 kDefaultTCPMSS),
      max_congestion_window_(max_tcp_congestion_window * kDefaultTCPMSS),
      min_congestion_window_(kDefaultMinimumCongestionWindow),
      high_gain_(kDefaultHighGain),
      high_cwnd_gain_(kDefaultHighGain),
      drain_gain_(1.f / kDefaultHighGain),
      pacing_rate_(QuicBandwidth::Zero()),
      pacing_gain_(1),
      congestion_window_gain_(1),
      congestion_window_gain_constant_(
          static_cast<float>(GetQuicFlag(FLAGS_quic_bbr_cwnd_gain))),
      num_startup_rtts_(kRoundTripsWithoutGrowthBeforeExitingStartup),
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
      recovery_state_(NOT_IN_RECOVERY),
      recovery_window_(max_congestion_window_),
      slower_startup_(false),
      rate_based_startup_(false),
      enable_ack_aggregation_during_startup_(false),
      expire_ack_aggregation_in_startup_(false),
      drain_to_target_(drain_to_target),
      detect_overshooting_(false),
      bytes_lost_while_detecting_overshooting_(0),
      bytes_lost_multiplier_while_detecting_overshooting_(2),
      cwnd_to_calculate_min_pacing_rate_(initial_congestion_window_),
      max_congestion_window_with_network_parameters_adjusted_(
          kMaxInitialCongestionWindow * kDefaultTCPMSS) {
  EnterStartupMode(now);
  set_high_cwnd_gain(kDerivedHighCWNDGain);
}

QuicBbrSender::~QuicBbrSender() {}

void QuicBbrSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  if (mode_ == STARTUP) {
    initial_congestion_window_ = congestion_window * kDefaultTCPMSS;
    congestion_window_ = congestion_window * kDefaultTCPMSS;
    cwnd_to_calculate_min_pacing_rate_ = std::min(
        initial_congestion_window_, cwnd_to_calculate_min_pacing_rate_);
  }
}

bool QuicBbrSender::InSlowStart() const {
  return mode_ == STARTUP;
}

void QuicBbrSender::OnPacketSent(ProtoTime sent_time,
                             QuicByteCount bytes_in_flight,
                             QuicPacketNumber packet_number,
                             QuicByteCount bytes,
                             HasRetransmittableData is_retransmittable) {
  /*if (stats_ && InSlowStart()) {
    ++stats_->slowstart_packets_sent;
    stats_->slowstart_bytes_sent += bytes;
  }*/

  last_sent_packet_ = packet_number;

  if (bytes_in_flight == 0 && sampler_.is_app_limited()) {
    exiting_quiescence_ = true;
  }

  sampler_.OnPacketSent(sent_time, packet_number, bytes, bytes_in_flight,
                        is_retransmittable);
}

/*void QuicBbrSender::OnPacketNeutered(QuicPacketNumber packet_number) {
  sampler_.OnPacketNeutered(packet_number);
}*/

bool QuicBbrSender::CanSend(QuicByteCount bytes_in_flight) {
  return bytes_in_flight < GetCongestionWindow();
}

QuicBandwidth QuicBbrSender::PacingRate(QuicByteCount /*bytes_in_flight*/) const {
  if (pacing_rate_.IsZero()) {
    return high_gain_ * QuicBandwidth::FromBytesAndTimeDelta(
                            initial_congestion_window_, GetMinRtt());
  }
  return pacing_rate_;
}

QuicBandwidth QuicBbrSender::BandwidthEstimate() const {
  return max_bandwidth_.GetBest();
}

QuicByteCount QuicBbrSender::GetCongestionWindow() const {
  if (mode_ == PROBE_RTT) {
    return ProbeRttCongestionWindow();
  }

  if (InRecovery()) {
    return std::min(congestion_window_, recovery_window_);
  }

  return congestion_window_;
}

QuicByteCount QuicBbrSender::GetSlowStartThreshold() const {
  return 0;
}

bool QuicBbrSender::InRecovery() const {
  return recovery_state_ != NOT_IN_RECOVERY;
}

bool QuicBbrSender::ShouldSendProbingPacket() const {
  if (pacing_gain_ <= 1) {
    return false;
  }

  // TODO(b/77975811): If the pipe is highly under-utilized, consider not
  // sending a probing transmission, because the extra bandwidth is not needed.
  // If flexible_app_limited is enabled, check if the pipe is sufficiently full.
  if (flexible_app_limited_) {
    return !IsPipeSufficientlyFull();
  } else {
    return true;
  }
}

bool QuicBbrSender::IsPipeSufficientlyFull() const {
  // See if we need more bytes in flight to see more bandwidth.
  if (mode_ == STARTUP) {
    // STARTUP exits if it doesn't observe a 25% bandwidth increase, so the CWND
    // must be more than 25% above the target.
    return unacked_packets_->bytes_in_flight() >=
           GetTargetCongestionWindow(1.5);
  }
  if (pacing_gain_ > 1) {
    // Super-unity PROBE_BW doesn't exit until 1.25 * BDP is achieved.
    return unacked_packets_->bytes_in_flight() >=
           GetTargetCongestionWindow(pacing_gain_);
  }
  // If bytes_in_flight are above the target congestion window, it should be
  // possible to observe the same or more bandwidth if it's available.
  return unacked_packets_->bytes_in_flight() >= GetTargetCongestionWindow(1.1);
}

void QuicBbrSender::AdjustNetworkParameters(QuicBandwidth bandwidth,
                                        TimeDelta rtt,
                                        bool allow_cwnd_to_decrease) {

}

void QuicBbrSender::OnCongestionEvent(bool /*rtt_updated*/,
                                  QuicByteCount prior_in_flight,
                                  ProtoTime event_time,
                                  const AckedPacketVector& acked_packets,
                                  const LostPacketVector& lost_packets) {
  const QuicByteCount total_bytes_acked_before = sampler_.total_bytes_acked();
  const QuicByteCount total_bytes_lost_before = sampler_.total_bytes_lost();

  bool is_round_start = false;
  bool min_rtt_expired = false;
  QuicByteCount excess_acked = 0;
  QuicByteCount bytes_lost = 0;

  // The send state of the largest packet in acked_packets, unless it is
  // empty. If acked_packets is empty, it's the send state of the largest
  // packet in lost_packets.
  QuicSendTimeState last_packet_send_state;

  if (!acked_packets.empty()) {
    QuicPacketNumber last_acked_packet = acked_packets.rbegin()->packet_number;
    is_round_start = UpdateRoundTripCounter(last_acked_packet);
    UpdateRecoveryState(last_acked_packet, !lost_packets.empty(),
                        is_round_start);
  }

  QuicBandwidthSamplerInterface::CongestionEventSample sample =
      sampler_.OnCongestionEvent(event_time, acked_packets, lost_packets,
                                 max_bandwidth_.GetBest(),
                                 QuicBandwidth::Infinite(), round_trip_count_);
  if (sample.last_packet_send_state.is_valid) {
    last_sample_is_app_limited_ = sample.last_packet_send_state.is_app_limited;
    has_non_app_limited_sample_ |= !last_sample_is_app_limited_;
    /*if (stats_) {
      stats_->has_non_app_limited_sample = has_non_app_limited_sample_;
    }*/
  }
  // Avoid updating |max_bandwidth_| if a) this is a loss-only event, or b) all
  // packets in |acked_packets| did not generate valid samples. (e.g. ack of
  // ack-only packets). In both cases, sampler_.total_bytes_acked() will not
  // change.
  if (total_bytes_acked_before != sampler_.total_bytes_acked()) {
    /*QUIC_LOG_IF(WARNING, sample.sample_max_bandwidth.IsZero())
        << sampler_.total_bytes_acked() - total_bytes_acked_before
        << " bytes from " << acked_packets.size()
        << " packets have been acked, but sample_max_bandwidth is zero.";*/
    if (!sample.sample_is_app_limited ||
        sample.sample_max_bandwidth > max_bandwidth_.GetBest()) {
      max_bandwidth_.Update(sample.sample_max_bandwidth, round_trip_count_);
    }
  }

  if (!sample.sample_rtt.IsInfinite()) {
    min_rtt_expired = MaybeUpdateMinRtt(event_time, sample.sample_rtt);
  }
  bytes_lost = sampler_.total_bytes_lost() - total_bytes_lost_before;
  if (mode_ == STARTUP) {
    /*if (stats_) {
      stats_->slowstart_packets_lost += lost_packets.size();
      stats_->slowstart_bytes_lost += bytes_lost;
    }*/
  }
  excess_acked = sample.extra_acked;
  last_packet_send_state = sample.last_packet_send_state;

  if (!lost_packets.empty()) {
    ++num_loss_events_in_round_;
    bytes_lost_in_round_ += bytes_lost;
  }

  // Handle logic specific to PROBE_BW mode.
  if (mode_ == PROBE_BW) {
    UpdateGainCyclePhase(event_time, prior_in_flight, !lost_packets.empty());
  }

  // Handle logic specific to STARTUP and DRAIN modes.
  if (is_round_start && !is_at_full_bandwidth_) {
    CheckIfFullBandwidthReached(last_packet_send_state);
  }
  MaybeExitStartupOrDrain(event_time);

  // Handle logic specific to PROBE_RTT.
  MaybeEnterOrExitProbeRtt(event_time, is_round_start, min_rtt_expired);

  // Calculate number of packets acked and lost.
  QuicByteCount bytes_acked =
      sampler_.total_bytes_acked() - total_bytes_acked_before;

  // After the model is updated, recalculate the pacing rate and congestion
  // window.
  CalculatePacingRate(bytes_lost);
  CalculateCongestionWindow(bytes_acked, excess_acked);
  CalculateRecoveryWindow(bytes_acked, bytes_lost);

  // Cleanup internal state.
  sampler_.RemoveObsoletePackets(unacked_packets_->GetLeastUnacked());
  if (is_round_start) {
    num_loss_events_in_round_ = 0;
    bytes_lost_in_round_ = 0;
  }
}

CongestionControlType QuicBbrSender::GetCongestionControlType() const {
  return kQuicBBR;
}

TimeDelta QuicBbrSender::GetMinRtt() const {
  if (!min_rtt_.IsZero()) {
    return min_rtt_;
  }
  // min_rtt could be available if the handshake packet gets neutered then
  // gets acknowledged. This could only happen for QUIC crypto where we do not
  // drop keys.
  return rtt_stats_->MinOrInitialRtt();
}

QuicByteCount QuicBbrSender::GetTargetCongestionWindow(float gain) const {
  QuicByteCount bdp = GetMinRtt() * BandwidthEstimate();
  QuicByteCount congestion_window = gain * bdp;

  // BDP estimate will be zero if no bandwidth samples are available yet.
  if (congestion_window == 0) {
    congestion_window = gain * initial_congestion_window_;
  }

  return std::max(congestion_window, min_congestion_window_);
}

QuicByteCount QuicBbrSender::ProbeRttCongestionWindow() const {
  return min_congestion_window_;
}

void QuicBbrSender::EnterStartupMode(ProtoTime now) {
  if (stats_) {
    ++stats_->slowstart_count;
    stats_->slowstart_duration.Start(now);
  }
  mode_ = STARTUP;
  pacing_gain_ = high_gain_;
  congestion_window_gain_ = high_cwnd_gain_;
}

void QuicBbrSender::EnterProbeBandwidthMode(ProtoTime now) {
  mode_ = PROBE_BW;
  congestion_window_gain_ = congestion_window_gain_constant_;

  // Pick a random offset for the gain cycle out of {0, 2..7} range. 1 is
  // excluded because in that case increased gain and decreased gain would not
  // follow each other.
  cycle_current_offset_ = random_->nextInt() % (kGainCycleLength - 1);
  if (cycle_current_offset_ >= 1) {
    cycle_current_offset_ += 1;
  }

  last_cycle_start_ = now;
  pacing_gain_ = kPacingGain[cycle_current_offset_];
}

bool QuicBbrSender::UpdateRoundTripCounter(QuicPacketNumber last_acked_packet) {
  if (!current_round_trip_end_.IsInitialized() ||
      last_acked_packet > current_round_trip_end_) {
    round_trip_count_++;
    current_round_trip_end_ = last_sent_packet_;
    if (stats_ && InSlowStart()) {
      ++stats_->slowstart_num_rtts;
    }
    return true;
  }

  return false;
}

bool QuicBbrSender::MaybeUpdateMinRtt(ProtoTime now,
                                  TimeDelta sample_min_rtt) {
  // Do not expire min_rtt if none was ever available.
  bool min_rtt_expired =
      !min_rtt_.IsZero() && (now > (min_rtt_timestamp_ + kMinRttExpiry));

  if (min_rtt_expired || sample_min_rtt < min_rtt_ || min_rtt_.IsZero()) {
    QUIC_DVLOG(2) << "Min RTT updated, old value: " << min_rtt_
                  << ", new value: " << sample_min_rtt
                  << ", current time: " << now.ToDebuggingValue();

    min_rtt_ = sample_min_rtt;
    min_rtt_timestamp_ = now;
  }
  DCHECK(!min_rtt_.IsZero());

  return min_rtt_expired;
}

void QuicBbrSender::UpdateGainCyclePhase(ProtoTime now,
                                     QuicByteCount prior_in_flight,
                                     bool has_losses) {
  const QuicByteCount bytes_in_flight = unacked_packets_->bytes_in_flight();
  // In most cases, the cycle is advanced after an RTT passes.
  bool should_advance_gain_cycling = now - last_cycle_start_ > GetMinRtt();

  // If the pacing gain is above 1.0, the connection is trying to probe the
  // bandwidth by increasing the number of bytes in flight to at least
  // pacing_gain * BDP.  Make sure that it actually reaches the target, as long
  // as there are no losses suggesting that the buffers are not able to hold
  // that much.
  if (pacing_gain_ > 1.0 && !has_losses &&
      prior_in_flight < GetTargetCongestionWindow(pacing_gain_)) {
    should_advance_gain_cycling = false;
  }

  // If pacing gain is below 1.0, the connection is trying to drain the extra
  // queue which could have been incurred by probing prior to it.  If the number
  // of bytes in flight falls down to the estimated BDP value earlier, conclude
  // that the queue has been successfully drained and exit this cycle early.
  if (pacing_gain_ < 1.0 && bytes_in_flight <= GetTargetCongestionWindow(1)) {
    should_advance_gain_cycling = true;
  }

  if (should_advance_gain_cycling) {
    cycle_current_offset_ = (cycle_current_offset_ + 1) % kGainCycleLength;
    if (cycle_current_offset_ == 0) {
      ++stats_->bbr_num_cycles;
    }
    last_cycle_start_ = now;
    // Stay in low gain mode until the target BDP is hit.
    // Low gain mode will be exited immediately when the target BDP is achieved.
    if (drain_to_target_ && pacing_gain_ < 1 &&
        kPacingGain[cycle_current_offset_] == 1 &&
        bytes_in_flight > GetTargetCongestionWindow(1)) {
      return;
    }
    pacing_gain_ = kPacingGain[cycle_current_offset_];
  }
}

void QuicBbrSender::CheckIfFullBandwidthReached(
    const QuicSendTimeState& last_packet_send_state) {
  if (last_sample_is_app_limited_) {
    return;
  }

  QuicBandwidth target = bandwidth_at_last_round_ * kStartupGrowthTarget;
  if (BandwidthEstimate() >= target) {
    bandwidth_at_last_round_ = BandwidthEstimate();
    rounds_without_bandwidth_gain_ = 0;
    if (expire_ack_aggregation_in_startup_) {
      // Expire old excess delivery measurements now that bandwidth increased.
      sampler_.ResetMaxAckHeightTracker(0, round_trip_count_);
    }
    return;
  }

  rounds_without_bandwidth_gain_++;
  if ((rounds_without_bandwidth_gain_ >= num_startup_rtts_) ||
      ShouldExitStartupDueToLoss(last_packet_send_state)) {
    DCHECK(has_non_app_limited_sample_);
    is_at_full_bandwidth_ = true;
  }
}

void QuicBbrSender::MaybeExitStartupOrDrain(ProtoTime now) {
  if (mode_ == STARTUP && is_at_full_bandwidth_) {
    OnExitStartup(now);
    mode_ = DRAIN;
    pacing_gain_ = drain_gain_;
    congestion_window_gain_ = high_cwnd_gain_;
  }
  if (mode_ == DRAIN &&
      unacked_packets_->bytes_in_flight() <= GetTargetCongestionWindow(1)) {
    EnterProbeBandwidthMode(now);
  }
}

void QuicBbrSender::OnExitStartup(ProtoTime now) {
  DCHECK_EQ(mode_, STARTUP);
  if (stats_) {
    stats_->slowstart_duration.Stop(now);
  }
}

bool QuicBbrSender::ShouldExitStartupDueToLoss(
    const QuicSendTimeState& last_packet_send_state) const {
  if (num_loss_events_in_round_ <
          GetQuicFlag(FLAGS_quic_bbr2_default_startup_full_loss_count) ||
      !last_packet_send_state.is_valid) {
    return false;
  }

  const QuicByteCount inflight_at_send = last_packet_send_state.bytes_in_flight;

  if (inflight_at_send > 0 && bytes_lost_in_round_ > 0) {
    if (bytes_lost_in_round_ >
        inflight_at_send *
            GetQuicFlag(FLAGS_quic_bbr2_default_loss_threshold)) {
      stats_->bbr_exit_startup_due_to_loss = true;
      return true;
    }
    return false;
  }

  return false;
}

void QuicBbrSender::MaybeEnterOrExitProbeRtt(ProtoTime now,
                                         bool is_round_start,
                                         bool min_rtt_expired) {
  if (min_rtt_expired && !exiting_quiescence_ && mode_ != PROBE_RTT) {
    if (InSlowStart()) {
      OnExitStartup(now);
    }
    mode_ = PROBE_RTT;
    pacing_gain_ = 1;
    // Do not decide on the time to exit PROBE_RTT until the |bytes_in_flight|
    // is at the target small value.
    exit_probe_rtt_at_ = ProtoTime::Zero();
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
          EnterProbeBandwidthMode(now);
        }
      }
    }
  }

  exiting_quiescence_ = false;
}

void QuicBbrSender::UpdateRecoveryState(QuicPacketNumber last_acked_packet,
                                    bool has_losses,
                                    bool is_round_start) {
  // Disable recovery in startup, if loss-based exit is enabled.
  if (!is_at_full_bandwidth_) {
    return;
  }

  // Exit recovery when there are no losses for a round.
  if (has_losses) {
    end_recovery_at_ = last_sent_packet_;
  }

  switch (recovery_state_) {
    case NOT_IN_RECOVERY:
      // Enter conservation on the first loss.
      if (has_losses) {
        recovery_state_ = CONSERVATION;
        // This will cause the |recovery_window_| to be set to the correct
        // value in CalculateRecoveryWindow().
        recovery_window_ = 0;
        // Since the conservation phase is meant to be lasting for a whole
        // round, extend the current round as if it were started right now.
        current_round_trip_end_ = last_sent_packet_;
      }
      break;

    case CONSERVATION:
      if (is_round_start) {
        recovery_state_ = GROWTH;
      }
      //QUIC_FALLTHROUGH_INTENDED;

    case GROWTH:
      // Exit recovery if appropriate.
      if (!has_losses && last_acked_packet > end_recovery_at_) {
        recovery_state_ = NOT_IN_RECOVERY;
      }

      break;
  }
}

void QuicBbrSender::CalculatePacingRate(QuicByteCount bytes_lost) {
  if (BandwidthEstimate().IsZero()) {
    return;
  }

  QuicBandwidth target_rate = pacing_gain_ * BandwidthEstimate();
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

  if (detect_overshooting_) {
    bytes_lost_while_detecting_overshooting_ += bytes_lost;
    // Check for overshooting with network parameters adjusted when pacing rate
    // > target_rate and loss has been detected.
    if (pacing_rate_ > target_rate &&
        bytes_lost_while_detecting_overshooting_ > 0) {
      if (has_non_app_limited_sample_ ||
          bytes_lost_while_detecting_overshooting_ *
                  bytes_lost_multiplier_while_detecting_overshooting_ >
              initial_congestion_window_) {
        // We are fairly sure overshoot happens if 1) there is at least one
        // non app-limited bw sample or 2) half of IW gets lost. Slow pacing
        // rate.
        pacing_rate_ = std::max(
            target_rate, QuicBandwidth::FromBytesAndTimeDelta(
                             cwnd_to_calculate_min_pacing_rate_, GetMinRtt()));
        if (stats_) {
          stats_->overshooting_detected_with_network_parameters_adjusted = true;
        }
        bytes_lost_while_detecting_overshooting_ = 0;
        detect_overshooting_ = false;
      }
    }
  }

  // Do not decrease the pacing rate during startup.
  pacing_rate_ = std::max(pacing_rate_, target_rate);
}

void QuicBbrSender::CalculateCongestionWindow(QuicByteCount bytes_acked,
                                          QuicByteCount excess_acked) {
  if (mode_ == PROBE_RTT) {
    return;
  }

  QuicByteCount target_window =
      GetTargetCongestionWindow(congestion_window_gain_);
  if (is_at_full_bandwidth_) {
    // Add the max recently measured ack aggregation to CWND.
    target_window += sampler_.max_ack_height();
  } else if (enable_ack_aggregation_during_startup_) {
    // Add the most recent excess acked.  Because CWND never decreases in
    // STARTUP, this will automatically create a very localized max filter.
    target_window += excess_acked;
  }

  // Instead of immediately setting the target CWND as the new one, BBR grows
  // the CWND towards |target_window| by only increasing it |bytes_acked| at a
  // time.
  const bool add_bytes_acked =
      !GetQuicReloadableFlag(quic_bbr_no_bytes_acked_in_startup_recovery) ||
      !InRecovery();
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

void QuicBbrSender::CalculateRecoveryWindow(QuicByteCount bytes_acked,
                                        QuicByteCount bytes_lost) {
  if (recovery_state_ == NOT_IN_RECOVERY) {
    return;
  }

  // Set up the initial recovery window.
  if (recovery_window_ == 0) {
    recovery_window_ = unacked_packets_->bytes_in_flight() + bytes_acked;
    recovery_window_ = std::max(min_congestion_window_, recovery_window_);
    return;
  }

  // Remove losses from the recovery window, while accounting for a potential
  // integer underflow.
  recovery_window_ = recovery_window_ >= bytes_lost
                         ? recovery_window_ - bytes_lost
                         : kMaxSegmentSize;

  // In CONSERVATION mode, just subtracting losses is sufficient.  In GROWTH,
  // release additional |bytes_acked| to achieve a slow-start-like behavior.
  if (recovery_state_ == GROWTH) {
    recovery_window_ += bytes_acked;
  }

  // Always allow sending at least |bytes_acked| in response.
  recovery_window_ = std::max(
      recovery_window_, unacked_packets_->bytes_in_flight() + bytes_acked);
  recovery_window_ = std::max(min_congestion_window_, recovery_window_);
}

std::string QuicBbrSender::GetDebugState() const {
  std::ostringstream stream;
  stream << ExportDebugState();
  return stream.str();
}

void QuicBbrSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {
  if (bytes_in_flight >= GetCongestionWindow()) {
    return;
  }
  if (flexible_app_limited_ && IsPipeSufficientlyFull()) {
    return;
  }

  sampler_.OnAppLimited();
  QUIC_DVLOG(2) << "Becoming application limited. Last sent packet: "
                << last_sent_packet_ << ", CWND: " << GetCongestionWindow();
}

/*void QuicBbrSender::PopulateConnectionStats(QuicConnectionStats* stats) const {
  stats->num_ack_aggregation_epochs = sampler_.num_ack_aggregation_epochs();
}*/

QuicBbrSender::DebugState QuicBbrSender::ExportDebugState() const {
  return DebugState(*this);
}

static std::string ModeToString(QuicBbrSender::Mode mode) {
  switch (mode) {
    case QuicBbrSender::STARTUP:
      return "STARTUP";
    case QuicBbrSender::DRAIN:
      return "DRAIN";
    case QuicBbrSender::PROBE_BW:
      return "PROBE_BW";
    case QuicBbrSender::PROBE_RTT:
      return "PROBE_RTT";
  }
  return "???";
}

std::ostream& operator<<(std::ostream& os, const QuicBbrSender::Mode& mode) {
  os << ModeToString(mode);
  return os;
}

std::ostream& operator<<(std::ostream& os, const QuicBbrSender::DebugState& state) {
  os << "Mode: " << ModeToString(state.mode) << std::endl;
  os << "Maximum bandwidth: " << state.max_bandwidth << std::endl;
  os << "Round trip counter: " << state.round_trip_count << std::endl;
  os << "Gain cycle index: " << static_cast<int>(state.gain_cycle_index)
     << std::endl;
  os << "Congestion window: " << state.congestion_window << " bytes"
     << std::endl;

  if (state.mode == QuicBbrSender::STARTUP) {
    os << "(startup) Bandwidth at last round: " << state.bandwidth_at_last_round
       << std::endl;
    os << "(startup) Rounds without gain: "
       << state.rounds_without_bandwidth_gain << std::endl;
  }

  os << "Minimum RTT: " << state.min_rtt << std::endl;
  os << "Minimum RTT timestamp: " << state.min_rtt_timestamp.ToDebuggingValue()
     << std::endl;

  os << "Last sample is app-limited: "
     << (state.last_sample_is_app_limited ? "yes" : "no");

  return os;
}

}  // namespace quic
