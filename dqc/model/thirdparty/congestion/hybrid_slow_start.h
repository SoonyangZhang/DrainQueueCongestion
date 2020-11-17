#pragma once
#include "proto_types.h"
#include "proto_time.h"
namespace dqc{
class HybridSlowStart {
 public:
  HybridSlowStart();
  HybridSlowStart(const HybridSlowStart&) = delete;
  HybridSlowStart& operator=(const HybridSlowStart&) = delete;

  void OnPacketAcked(QuicPacketNumber acked_packet_number);

  void OnPacketSent(QuicPacketNumber packet_number);

  // ShouldExitSlowStart should be called on every new ack frame, since a new
  // RTT measurement can be made then.
  // rtt: the RTT for this ack packet.
  // min_rtt: is the lowest delay (RTT) we have seen during the session.
  // congestion_window: the congestion window in packets.
  bool ShouldExitSlowStart(TimeDelta rtt,
                           TimeDelta min_rtt,
                           QuicPacketCount congestion_window);

  // Start a new slow start phase.
  void Restart();

  // TODO(ianswett): The following methods should be private, but that requires
  // a follow up CL to update the unit test.
  // Returns true if this ack the last packet number of our current slow start
  // round.
  // Call Reset if this returns true.
  bool IsEndOfRound(QuicPacketNumber ack) const;

  // Call for the start of each receive round (burst) in the slow start phase.
  void StartReceiveRound(QuicPacketNumber last_sent);

  // Whether slow start has started.
  bool started() const { return started_; }

 private:
  // Whether a condition for exiting slow start has been found.
  enum HystartState {
    NOT_FOUND,
    DELAY,  // Too much increase in the round's min_rtt was observed.
  };

  // Whether the hybrid slow start has been started.
  bool started_;
  HystartState hystart_found_;
  // Last packet number sent which was CWND limited.
  QuicPacketNumber last_sent_packet_number_;

  // Variables for tracking acks received during a slow start round.
  QuicPacketNumber end_packet_number_;  // End of the receive round.
  uint32_t rtt_sample_count_;  // Number of rtt samples in the current round.
  TimeDelta current_min_rtt_;  // The minimum rtt of current round.
};
}
