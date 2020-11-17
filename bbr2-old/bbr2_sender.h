#pragma once
#include <cstdint>
#include "bbr2_misc.h"
#include "bbr2_drain.h"
#include "bbr2_probe_bw.h"
#include "bbr2_probe_rtt.h"
#include "bbr2_startup.h"
#include "random.h"
namespace dqc {

class QUIC_EXPORT_PRIVATE Bbr2Sender final : public SendAlgorithmInterface {
 public:
  Bbr2Sender(ProtoTime now,
             const RttStats* rtt_stats,
             const UnackedPacketMap* unacked_packets,
             QuicPacketCount initial_cwnd_in_packets,
             QuicPacketCount max_cwnd_in_packets,
             Random* random,
             QuicConnectionStats* /*stats*/);

  ~Bbr2Sender() override = default;

  // Start implementation of SendAlgorithmInterface.
  bool InSlowStart() const override { return mode_ == Bbr2Mode::STARTUP; }

  bool InRecovery() const override {
    // TODO(wub): Implement Recovery.
    return false;
  }

  bool ShouldSendProbingPacket() const override;

  //void SetFromConfig(const QuicConfig& config,
  //                   Perspective perspective) override;

  void AdjustNetworkParameters(QuicBandwidth bandwidth,
                               TimeDelta rtt,
                               bool allow_cwnd_to_decrease) override;
  void SetNumEmulatedConnections(int num_connections) override {}
  void SetInitialCongestionWindowInPackets(
      QuicPacketCount congestion_window) override;

  void OnCongestionEvent(bool rtt_updated,
                         QuicByteCount prior_in_flight,
                         ProtoTime event_time,
                         const AckedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets) override;

  void OnPacketSent(ProtoTime sent_time,
                    QuicByteCount bytes_in_flight,
                    QuicPacketNumber packet_number,
                    QuicByteCount bytes,
                    HasRetransmittableData is_retransmittable) override;

  void OnRetransmissionTimeout(bool /*packets_retransmitted*/) override {}

  void OnConnectionMigration() override {}

  bool CanSend(QuicByteCount bytes_in_flight) override;

  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const override;

  QuicBandwidth BandwidthEstimate() const override {
    return model_.BandwidthEstimate();
  }

  QuicByteCount GetCongestionWindow() const override;

  QuicByteCount GetSlowStartThreshold() const override { return 0; }

  CongestionControlType GetCongestionControlType() const override {
    return kBBRv2;
  }

  std::string GetDebugState() const override;

  void OnApplicationLimited(QuicByteCount bytes_in_flight) override;
  // End implementation of SendAlgorithmInterface.

  const Bbr2Params& Params() const { return params_; }

  QuicByteCount GetMinimumCongestionWindow() const {
    return cwnd_limits().Min();
  }

  struct DebugState {
    Bbr2Mode mode;

    // Shared states.
    QuicRoundTripCount round_trip_count;
    QuicBandwidth bandwidth_hi = QuicBandwidth::Zero();
    QuicBandwidth bandwidth_lo = QuicBandwidth::Zero();
    QuicBandwidth bandwidth_est = QuicBandwidth::Zero();
    TimeDelta min_rtt = TimeDelta::Zero();
    ProtoTime min_rtt_timestamp = ProtoTime::Zero();
    QuicByteCount congestion_window;
    QuicBandwidth pacing_rate = QuicBandwidth::Zero();
    bool last_sample_is_app_limited;
    QuicPacketNumber end_of_app_limited_phase;

    // Mode-specific debug states.
    Bbr2StartupMode::DebugState startup;
    Bbr2DrainMode::DebugState drain;
    Bbr2ProbeBwMode::DebugState probe_bw;
    Bbr2ProbeRttMode::DebugState probe_rtt;
  };

  DebugState ExportDebugState() const;

 private:
  void UpdatePacingRate(QuicByteCount bytes_acked);
  void UpdateCongestionWindow(QuicByteCount bytes_acked);
  QuicByteCount GetTargetCongestionWindow(float gain) const;

  // Helper function for BBR2_MODE_DISPATCH.
  Bbr2ProbeRttMode& probe_rtt_or_die() {
    DCHECK_EQ(mode_, Bbr2Mode::PROBE_RTT);
    return probe_rtt_;
  }

  const Bbr2ProbeRttMode& probe_rtt_or_die() const {
    DCHECK_EQ(mode_, Bbr2Mode::PROBE_RTT);
    return probe_rtt_;
  }

  uint64_t RandomUint64(uint64_t max) const {
    return random_->nextInt() % max;
  }

  // Returns true if there are enough bytes in flight to ensure more bandwidth
  // will be observed if present.
  bool IsPipeSufficientlyFull() const;

  // Cwnd limits imposed by the current Bbr2 mode.
  Limits<QuicByteCount> GetCwndLimitsByMode() const;

  // Cwnd limits imposed by caller.
  const Limits<QuicByteCount>& cwnd_limits() const;

  Bbr2Mode mode_;

  const RttStats* const rtt_stats_;
  const UnackedPacketMap* const unacked_packets_;
  Random* random_;

  const Bbr2Params params_;

  Bbr2NetworkModel model_;

  // Current cwnd and pacing rate.
  QuicByteCount cwnd_;
  QuicBandwidth pacing_rate_;

  Bbr2StartupMode startup_;
  Bbr2DrainMode drain_;
  Bbr2ProbeBwMode probe_bw_;
  Bbr2ProbeRttMode probe_rtt_;

  // Indicates app-limited calls should be ignored as long as there's
  // enough data inflight to see more bandwidth when necessary.
  bool flexible_app_limited_;

  // Debug only.
  bool last_sample_is_app_limited_;

  friend class Bbr2StartupMode;
  friend class Bbr2DrainMode;
  friend class Bbr2ProbeBwMode;
  friend class Bbr2ProbeRttMode;
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    const Bbr2Sender::DebugState& state);

}  // namespace quic
