#pragma once
#include "proto_types.h"
#include "proto_time.h"
namespace dqc{
class  CubicPlusBytes {
 public:
  explicit CubicPlusBytes(const ProtoClock* clock);
  CubicPlusBytes(const CubicPlusBytes&) = delete;
  CubicPlusBytes& operator=(const CubicPlusBytes&) = delete;

  void SetNumConnections(int num_connections);

  // Call after a timeout to reset the cubic state.
  void ResetCubicState();
  void UpdateCongestionWindowEstimate(QuicByteCount estimated_congestion_window,
		  QuicByteCount max_congestion_window);
  // Compute a new congestion window to use after a loss event.
  // Returns the new congestion window in packets. The new congestion window is
  // a multiplicative decrease of our current window.
  QuicByteCount CongestionWindowAfterPacketLoss(QuicPacketCount current);

  // Compute a new congestion window to use after a received ACK.
  // Returns the new congestion window in bytes. The new congestion window
  // follows a cubic function that depends on the time passed since last packet
  // loss.
  QuicByteCount CongestionWindowAfterAck(QuicByteCount acked_bytes,
                                         QuicByteCount current,
                                         TimeDelta delay_min,
                                         ProtoTime event_time);

  // Call on ack arrival when sender is unable to use the available congestion
  // window. Resets Cubic state during quiescence.
  void OnApplicationLimited();

 private:
  //friend class test::CubicBytesTest;

  static const TimeDelta MaxCubicTimeInterval() {
    return TimeDelta::FromMilliseconds(30);
  }

  // Compute the TCP Cubic alpha, beta, and beta-last-max based on the
  // current number of connections.
  float Alpha() const;
  float Beta() const;
  float BetaLastMax() const;

  QuicByteCount last_max_congestion_window() const {
    return last_max_congestion_window_;
  }

  const ProtoClock* clock_;

  // Number of connections to simulate.
  int num_connections_;

  // Time when this cycle started, after last loss event.
  ProtoTime epoch_;

  // Max congestion window used just before last loss event.
  // Note: to improve fairness to other streams an additional back off is
  // applied to this value if the new value is below our latest value.
  QuicByteCount last_max_congestion_window_;

  // Number of acked bytes since the cycle started (epoch).
  QuicByteCount acked_bytes_count_;

  // TCP Reno equivalent congestion window in packets.
  QuicByteCount estimated_tcp_congestion_window_;

  // Origin point of cubic function.
  QuicByteCount origin_point_congestion_window_;

  // Time to origin point of cubic function in 2^10 fractions of a second.
  uint32_t time_to_origin_point_;

  // Last congestion window in packets computed by cubic function.
  QuicByteCount last_target_congestion_window_;
};
}
