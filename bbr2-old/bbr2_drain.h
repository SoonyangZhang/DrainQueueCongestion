#pragma once
#include "bbr2_misc.h"
namespace dqc {

class Bbr2Sender;
class QUIC_EXPORT_PRIVATE Bbr2DrainMode final : public Bbr2ModeBase {
 public:
  using Bbr2ModeBase::Bbr2ModeBase;

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

  bool IsProbingForBandwidth() const override { return false; }

  struct DebugState {
    QuicByteCount drain_target;
  };

  DebugState ExportDebugState() const;

 private:
  const Bbr2Params& Params() const;

  QuicByteCount DrainTarget() const;
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    const Bbr2DrainMode::DebugState& state);

}  // namespace quic