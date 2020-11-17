#pragma once
#include "bbr2_misc.h"
namespace dqc {

class Bbr2Sender;
class QUIC_EXPORT_PRIVATE Bbr2ProbeRttMode final : public Bbr2ModeBase {
 public:
  //using Bbr2ModeBase::Bbr2ModeBase;
  Bbr2ProbeRttMode(const Bbr2Sender* sender,
                                 Bbr2NetworkModel* model)
    : Bbr2ModeBase(sender, model){}
  void Enter(const Bbr2CongestionEvent& congestion_event) override;

  Bbr2Mode OnCongestionEvent(
      QuicByteCount prior_in_flight,
      ProtoTime event_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      const Bbr2CongestionEvent& congestion_event) override;

  Limits<QuicByteCount> GetCwndLimits() const override;

  bool IsProbingForBandwidth() const override { return false; }

  struct DebugState {
    QuicByteCount inflight_target;
    ProtoTime exit_time = ProtoTime::Zero();
  };

  DebugState ExportDebugState() const;

 private:
  const Bbr2Params& Params() const;

  QuicByteCount InflightTarget() const;

  ProtoTime exit_time_ = ProtoTime::Zero();
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    const Bbr2ProbeRttMode::DebugState& state);

}  // namespace quic
