#pragma once
#include <ostream>
#include "proto_send_algorithm_interface.h"
#include "proto_bandwidth_sampler.h"
#include "proto_windowed_filter.h"
#include "logging.h"
namespace dqc{
/*
CopaSender implements copa congestion control (Copa: Practical Delay-Based Congestion Control for the Internet).
The implementation is copied from mvfst project(https://github.com/facebookincubator/mvfst/).
*/
class RttStats;
class CopaSender : public SendAlgorithmInterface {
public:
  CopaSender(ProtoTime now,
            const RttStats* rtt_stats,
            const UnackedPacketMap* unacked_packets,
            QuicPacketCount initial_tcp_congestion_window,
            QuicPacketCount max_tcp_congestion_window,
            Random* random);
  CopaSender(const CopaSender&) = delete;
  CopaSender& operator=(const CopaSender&) = delete;
  ~CopaSender() override;

  // Start implementation of SendAlgorithmInterface.
  bool InSlowStart() const override;
  bool InRecovery() const override;
  bool ShouldSendProbingPacket() const override {return false;};


  void AdjustNetworkParameters(QuicBandwidth bandwidth,
                               TimeDelta rtt,
                               bool allow_cwnd_to_decrease) override{};
  void SetNumEmulatedConnections(int num_connections) override {}
  void SetInitialCongestionWindowInPackets(
      QuicPacketCount congestion_window) override{};
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
  void OnRetransmissionTimeout(bool packets_retransmitted) override {}
  void OnConnectionMigration() override {}
  bool CanSend(QuicByteCount bytes_in_flight) override;
  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const override;
  QuicBandwidth BandwidthEstimate() const override;
  QuicByteCount GetCongestionWindow() const override;
  QuicByteCount GetSlowStartThreshold() const override;
  CongestionControlType GetCongestionControlType() const override;
  std::string GetDebugState() const override;
  void OnApplicationLimited(QuicByteCount bytes_in_flight) override;

private:
  struct VelocityState {
    uint64_t velocity{1};
    enum Direction {
      None,
      Up, // cwnd is increasing
      Down, // cwnd is decreasing
    };
    Direction direction{None};
    // number of rtts direction has remained same
    uint64_t numTimesDirectionSame{0};
    // updated every srtt
    QuicByteCount lastRecordedCwndBytes;
    ProtoTime lastCwndRecordTime{ProtoTime::Zero()};
  };
  void OnPacketLost(QuicPacketNumber largest_loss,
                    QuicByteCount lost_bytes,
                    QuicByteCount prior_in_flight);
  void OnPacketAcked(const AckedPacketVector&acked_packets,
                     QuicByteCount prior_in_flight,
                     ProtoTime event_time);
  void CheckAndUpdateDirection(const ProtoTime ackTime);
  void ChangeDirection(VelocityState::Direction newDirection,
                        const ProtoTime ackTime);
  // Determines the appropriate pacing rate for the connection.
  void CalculatePacingRate();
  const RttStats* rtt_stats_;
  const UnackedPacketMap* unacked_packets_;
  Random* random_;
  // Track the largest packet that has been sent.
  QuicPacketNumber largest_sent_packet_number_;
  // Track the largest packet that has been acked.
  QuicPacketNumber largest_acked_packet_number_;
  // Track the largest packet number outstanding when a CWND cutback occurs.
  QuicPacketNumber largest_sent_at_last_cutback_;
  // The maximum allowed number of bytes in flight.
  QuicByteCount congestion_window_;
  // The initial value of the |congestion_window_|.
  QuicByteCount initial_congestion_window_;
  // The largest value the |congestion_window_| can achieve.
  QuicByteCount max_congestion_window_;
  // The smallest value the |congestion_window_| can achieve.
  QuicByteCount min_congestion_window_;
  // The current pacing rate of the connection.
  QuicBandwidth pacing_rate_;
  bool isSlowStart_;
  ProtoTime lastCwndDoubleTime_;
  typedef WindowedFilter<TimeDelta,MinFilter<TimeDelta>,uint64_t,uint64_t> RTTFilter;
  RTTFilter minRTTFilter_;
  RTTFilter standingRTTFilter_;
  VelocityState velocityState_;
  /**
   * latencyFactor_ determines how latency sensitive the algorithm is. Lower
   * means it will maximime throughput at expense of delay. Higher value means
   * it will minimize delay at expense of throughput.
   */
  double latencyFactor_{0.50};
};
}

