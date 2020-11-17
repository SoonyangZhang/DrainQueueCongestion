#pragma once
#include "proto_types.h"
#include "hybrid_slow_start.h"
#include "prr_sender.h"
#include "cubic_plus_bytes.h"
#include "proto_bandwidth_sampler.h"
#include "proto_windowed_filter.h"
#include "proto_send_algorithm_interface.h"
namespace dqc{
class RttStats;
class CubicPlusSender : public SendAlgorithmInterface {
 public:
	  enum Mode {
	    // Startup phase of the connection.
	    STARTUP,
	    // After achieving the highest possible bandwidth during the startup, lower
	    // the pacing rate in order to drain the queue.
	    DRAIN,
	    // Cruising mode.
	    PROBE_BW,
	    // Temporarily slow down sending in order to empty the buffer and measure
	    // the real minimum RTT.
	    PROBE_RTT,
	  };
  CubicPlusSender(const ProtoClock* clock,
                      const RttStats* rtt_stats,
					  const UnackedPacketMap* unacked_packets,
                      bool reno,
                      QuicPacketCount initial_tcp_congestion_window,
                      QuicPacketCount max_congestion_window,
                      QuicConnectionStats* stats,
					  Random* random);
  CubicPlusSender(const CubicPlusSender&) = delete;
  CubicPlusSender& operator=(const CubicPlusSender&) = delete;
  ~CubicPlusSender() override;

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
  // End implementation of SendAlgorithmInterface.

  QuicByteCount min_congestion_window() const { return min_congestion_window_; }
  TimeDelta GetMinRtt() const;
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
 private:
 QuicBandwidth BandwidthEstimateFromSample() const;
 QuicByteCount GetTargetCongestionWindow(float gain) const;
 QuicByteCount ProbeRttCongestionWindow() const;
 void DiscardLostPackets(const LostPacketVector& lost_packets);
 bool UpdateRoundTripCounter(QuicPacketNumber last_acked_packet);
 bool UpdateBandwidthAndMinRtt(ProtoTime now,
                               const AckedPacketVector& acked_packets);
 void UpdateGainCyclePhase(ProtoTime now,
                           QuicByteCount prior_in_flight,
                           bool has_losses);
 // Tracks for how many round-trips the bandwidth has not increased
 // significantly.
 void CheckIfFullBandwidthReached();
 // Transitions from STARTUP to DRAIN and from DRAIN to PROBE_BW if
 // appropriate.
 void MaybeExitStartupOrDrain(ProtoTime now);
 // Decides whether to enter or exit PROBE_RTT.
 void MaybeEnterOrExitProbeRtt(ProtoTime now,
                               bool is_round_start,
                               bool min_rtt_expired);
 bool ShouldExtendMinRttExpiry() const;
 // Enters the STARTUP mode.
 void EnterStartupMode(ProtoTime now);
 // Enters the PROBE_BW mode.
 void EnterProbeBandwidthMode(ProtoTime now);
 void SwitchProbeRttToProbeBandwidth(ProtoTime now);
 // Determines the appropriate pacing rate for the connection.
 QuicByteCount UpdateAckAggregationBytes(ProtoTime ack_time,
                                         QuicByteCount newly_acked_bytes);
 void CalculatePacingRate();
 // Determines the appropriate congestion window for the connection.
 void CalculateCongestionWindow(QuicByteCount bytes_acked,
                                QuicByteCount excess_acked);
 void ResetCubicCongestionWindow(QuicByteCount start,QuicByteCount target);
 QuicByteCount GetBestCongestionWindow() const;
 typedef WindowedFilter<QuicBandwidth,
                         MaxFilter<QuicBandwidth>,
                         QuicRoundTripCount,
                         QuicRoundTripCount>
      MaxBandwidthFilter;
 typedef WindowedFilter<QuicByteCount,
                        MaxFilter<QuicByteCount>,
                        QuicRoundTripCount,
                        QuicRoundTripCount>
     MaxAckHeightFilter;
 const RttStats* rtt_stats_;
 QuicConnectionStats* stats_;
 const UnackedPacketMap* unacked_packets_;
 Random* random_;
 Mode mode_;
 // Bandwidth sampler provides BBR with the bandwidth measurements at
 // individual points.
 BandwidthSampler sampler_;
 // The number of the round trips that have occurred during the connection.
 QuicRoundTripCount round_trip_count_;

 // The packet number of the most recently sent packet.
 //QuicPacketNumber last_sent_packet_;
 QuicPacketNumber current_round_trip_end_;
 MaxBandwidthFilter max_bandwidth_;

 // Tracks the maximum number of bytes acked faster than the sending rate.
 MaxAckHeightFilter max_ack_height_;
// monitor the max congestion window in cubic, add by zsy
 MaxAckHeightFilter max_window_height_;
 // The time this aggregation started and the number of bytes acked during it.
 ProtoTime aggregation_epoch_start_time_;
 QuicByteCount aggregation_epoch_bytes_;
 // When true, add the most recent ack aggregation measurement during STARTUP.
 bool enable_ack_aggregation_during_startup_;
 // When true, expire the windowed ack aggregation values in STARTUP when
 // bandwidth increases more than 25%.
 bool expire_ack_aggregation_in_startup_;

 bool probe_rtt_based_on_bdp_;
 // When true, pace at 1.5x and disable packet conservation in STARTUP.
 bool slower_startup_;
 // When true, disables packet conservation in STARTUP.
 bool rate_based_startup_;
 // When non-zero, decreases the rate in STARTUP by the total number of bytes
 // lost in STARTUP divided by CWND.
 uint8_t startup_rate_reduction_multiplier_;
 // Sum of bytes lost in STARTUP.
 QuicByteCount startup_bytes_lost_;
 QuicPacketNumber end_recovery_at_;
 TimeDelta min_rtt_;
 ProtoTime min_rtt_timestamp_;
 float high_gain_;
 float high_cwnd_gain_;
 float drain_gain_;
 QuicBandwidth pacing_rate_;
 float pacing_gain_;
 float congestion_window_gain_;
 // The number of RTTs to stay in STARTUP mode.  Defaults to 3.
 QuicRoundTripCount num_startup_rtts_;
 // If true, exit startup if 1RTT has passed with no bandwidth increase and
 // the connection is in recovery.
 bool exit_startup_on_loss_;

 // Number of round-trips in PROBE_BW mode, used for determining the current
 // pacing gain cycle.
 int cycle_current_offset_;
 // The time at which the last pacing gain cycle was started.
 ProtoTime last_cycle_start_;

 // Indicates whether the connection has reached the full bandwidth mode.
 bool is_at_full_bandwidth_;
 // Number of rounds during which there was no significant bandwidth increase.
 QuicRoundTripCount rounds_without_bandwidth_gain_;
 // The bandwidth compared to which the increase is measured.
 QuicBandwidth bandwidth_at_last_round_;
 // Set to true upon exiting quiescence.
   bool exiting_quiescence_;

   // Time at which PROBE_RTT has to be exited.  Setting it to zero indicates
   // that the time is yet unknown as the number of packets in flight has not
   // reached the required value.
   ProtoTime exit_probe_rtt_at_;
   // Indicates whether a round-trip has passed since PROBE_RTT became active.
   bool probe_rtt_round_passed_;
   QuicByteCount congestion_window_before_probe_rtt_;
   // Indicates whether the most recent bandwidth sample was marked as
   // app-limited.
   bool last_sample_is_app_limited_;
   // Indicates whether any non app-limited samples have been recorded.
   bool has_non_app_limited_sample_;
   // Indicates app-limited calls should be ignored as long as there's
   // enough data inflight to see more bandwidth when necessary.
   bool flexible_app_limited_;

  PrrSender prr_;


  // If true, Reno congestion control is used instead of Cubic.
  const bool reno_;

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

  CubicPlusBytes cubic_;

  // ACK counter for the Reno implementation.
  uint64_t num_acked_packets_;

  // Congestion window in bytes.
  QuicByteCount congestion_window_;
  QuicByteCount initial_congestion_window_;
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

  //from bbr
  bool probe_rtt_skipped_if_similar_rtt_;
  // If true, disable PROBE_RTT entirely as long as the connection was recently
  // app limited.
  bool probe_rtt_disabled_if_app_limited_;
  bool app_limited_since_last_probe_rtt_;
  TimeDelta min_rtt_since_last_probe_rtt_;
  TimeDelta min_rtt_expiration_{TimeDelta::FromMilliseconds(2500)};
  // Latched value of --quic_always_get_bw_sample_when_acked.
  const bool always_get_bw_sample_when_acked_;
};
}
