#pragma once
#include <list>
#include "proto_types.h"
#include "hybrid_slow_start.h"
#include "prr_sender.h"
#include "proto_send_algorithm_interface.h"
//implementation of
//Improving the Performance of Multipath Congestion Control over Wireless Networks
namespace dqc{
class RttStats;
class MpVenoSender : public SendAlgorithmInterface {
 public:
    struct mptcp_olia {
        mptcp_olia():mptcp_loss1(0),
        mptcp_loss2(0),
        mptcp_loss3(0),
        epsilon_num(0),
        epsilon_den(1),
        mptcp_snd_cwnd_cnt(0){}
        uint64_t    mptcp_loss1;
        uint64_t    mptcp_loss2;
        uint64_t    mptcp_loss3;
        int	epsilon_num;
        uint32_t    epsilon_den;
        int mptcp_snd_cwnd_cnt;
    };
  MpVenoSender(const ProtoClock* clock,
                      const RttStats* rtt_stats,
                      QuicPacketCount initial_tcp_congestion_window,
                      QuicPacketCount max_congestion_window,
                      QuicConnectionStats* stats);
  MpVenoSender(const MpVenoSender&) = delete;
  MpVenoSender& operator=(const MpVenoSender&) = delete;
  ~MpVenoSender() override;

  // Start implementation of SendAlgorithmInterface.
  //void SetFromConfig(const QuicConfig& config,
  //                   Perspective perspective) override;
  void AdjustNetworkParameters(QuicBandwidth bandwidth,
                               TimeDelta rtt,
                               bool allow_cwnd_to_decrease) override;
  void SetNumEmulatedConnections(int num_connections) override;
  void SetInitialCongestionWindowInPackets(
      QuicPacketCount congestion_window) override;
  void OnConnectionMigration() override;
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
  void OnRetransmissionTimeout(bool packets_retransmitted) override;
  bool CanSend(QuicByteCount bytes_in_flight) override;
  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const override;
  QuicBandwidth BandwidthEstimate() const override;
  QuicByteCount GetCongestionWindow() const override;
  QuicByteCount GetSlowStartThreshold() const override;
  CongestionControlType GetCongestionControlType() const override;
  bool InSlowStart() const override;
  bool InRecovery() const override;
  bool ShouldSendProbingPacket() const override;
  std::string GetDebugState() const override;
  void OnApplicationLimited(QuicByteCount bytes_in_flight) override;
  void SetCongestionId(uint32_t cid) override;
  uint32_t GetCongestionId() override{ return congestion_id_;}
  void RegisterCoupleCC(SendAlgorithmInterface*cc) override;
  void UnRegisterCoupleCC(SendAlgorithmInterface*cc) override;
  // End implementation of SendAlgorithmInterface.
  struct mptcp_olia* get_ca();
  uint64_t get_srtt_us();
  uint32_t mptcp_get_crt_cwnd();
  uint64_t mptcp_get_rate();
  void mptcp_get_epsilon();
  QuicByteCount min_congestion_window() const { return min_congestion_window_; }
 protected:
  // Compute the TCP Reno beta based on the current number of connections.
  float RenoBeta() const;

  bool IsCwndLimited(QuicByteCount bytes_in_flight) const;

  // TODO(ianswett): Remove these and migrate to OnCongestionEvent.
  void OnPacketAcked(QuicPacketNumber acked_packet_number,
                     QuicByteCount acked_bytes,
                     QuicByteCount prior_in_flight,
                     ProtoTime event_time);
  void SetCongestionWindowFromBandwidthAndRtt(QuicBandwidth bandwidth,
                                              TimeDelta rtt);
  void SetMinCongestionWindowInPackets(QuicPacketCount congestion_window);
  void ExitSlowstart();
  void OnPacketLost(QuicPacketNumber largest_loss,
                    QuicByteCount lost_bytes,
                    QuicByteCount prior_in_flight);
  void MaybeIncreaseCwnd(QuicPacketNumber acked_packet_number,
                         QuicByteCount acked_bytes,
                         QuicByteCount prior_in_flight,
                         ProtoTime event_time);
  void HandleRetransmissionTimeout();
  void UpdateTheta();
 private:
  //friend class test::TcpCubicSenderBytesPeer;

  HybridSlowStart hybrid_slow_start_;
  PrrSender prr_;
  const RttStats* rtt_stats_;
  QuicConnectionStats* stats_;


  // Number of connections to simulate.
  uint32_t num_connections_;

  // Track the largest packet that has been sent.
  QuicPacketNumber largest_sent_packet_number_;

  // Track the largest packet that has been acked.
  QuicPacketNumber largest_acked_packet_number_;

  // Track the largest packet number outstanding when a CWND cutback occurs.
  QuicPacketNumber largest_sent_at_last_cutback_;

  // Whether to use 4 packets as the actual min, but pace lower.
  bool min4_mode_;

  // Whether the last loss event caused us to exit slowstart.
  // Used for stats collection of slowstart_packets_lost
  bool last_cutback_exited_slowstart_;

  // When true, exit slow start with large cutback of congestion window.
  bool slow_start_large_reduction_;

  // When true, use unity pacing instead of PRR.
  bool no_prr_;

  // ACK counter for the Reno implementation.
  uint64_t num_acked_packets_;

  // Congestion window in bytes.
  QuicByteCount congestion_window_;

  // Minimum congestion window in bytes.
  QuicByteCount min_congestion_window_;

  // Maximum congestion window in bytes.
  QuicByteCount max_congestion_window_;

  // Slow start congestion window in bytes, aka ssthresh.
  QuicByteCount slowstart_threshold_;

  // Initial TCP congestion window in bytes. This variable can only be set when
  // this algorithm is created.
  const QuicByteCount initial_tcp_congestion_window_;

  // Initial maximum TCP congestion window in bytes. This variable can only be
  // set when this algorithm is created.
  const QuicByteCount initial_max_tcp_congestion_window_;

  // The minimum window when exiting slow start with large reduction.
  QuicByteCount min_slow_start_exit_window_;
  uint32_t congestion_id_{0};
  std::list<MpVenoSender*> other_ccs_;
  mptcp_olia ca_;
  
  uint32_t count_rtt_{0};
  TimeDelta min_rtt_;
  TimeDelta base_rtt_;
  uint32_t veno_diff_{0};
  uint32_t veno_beta_;
  float theta_{1.0};
};
}
