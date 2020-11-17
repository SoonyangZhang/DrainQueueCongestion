#pragma once
#include "bbr2_misc.h"
namespace dqc {

class Bbr2Sender;
class QUIC_EXPORT_PRIVATE Bbr2StartupMode final : public Bbr2ModeBase {
 public:
  Bbr2StartupMode(const Bbr2Sender* sender, Bbr2NetworkModel* model);

  void Enter(const Bbr2CongestionEvent& congestion_event) override;

  Bbr2Mode OnCongestionEvent(
      QuicByteCount prior_in_flight,
      ProtoTime event_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      const Bbr2CongestionEvent& congestion_event) override;

  Limits<QuicByteCount> GetCwndLimits() const override {
    return NoGreaterThan(model_->inflight_lo());
  }

  bool IsProbingForBandwidth() const override { return true; }

  bool FullBandwidthReached() const { return full_bandwidth_reached_; }

  struct DebugState {
    bool full_bandwidth_reached;
    QuicBandwidth full_bandwidth_baseline = QuicBandwidth::Zero();
    QuicRoundTripCount round_trips_without_bandwidth_growth;
  };

  DebugState ExportDebugState() const;

 private:
  const Bbr2Params& Params() const;

  void CheckFullBandwidthReached(const Bbr2CongestionEvent& congestion_event);

  void CheckExcessiveLosses(const LostPacketVector& lost_packets,
                            const Bbr2CongestionEvent& congestion_event);

  bool full_bandwidth_reached_;
  QuicBandwidth full_bandwidth_baseline_;
  QuicRoundTripCount rounds_without_bandwidth_growth_;

  // Number of loss events in the current round trip.
  int64_t loss_events_in_round_;
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    const Bbr2StartupMode::DebugState& state);

}  // namespace quic