#pragma once
#include "basic_macro.h"
#include <stdint.h>
#include "proto_time.h"
namespace dqc{
class RttStats
{
  public:
    RttStats();

    // Updates the RTT from an incoming ack which is received |send_delta| after
    // the packet is sent and the peer reports the ack being delayed |ack_delay|.
    // Time Unit: ms
    void UpdateRtt(TimeDelta send_delta, TimeDelta ack_delay, ProtoTime now);
    // Causes the smoothed_rtt to be increased to the latest_rtt if the latest_rtt
    // is larger. The mean deviation is increased to the most recent deviation if
    // it's larger.
    void ExpireSmoothedMetrics();

    // Called when connection migrates and rtt measurement needs to be reset.
    void OnConnectionMigration();
    //for resample rtt
    void Reset() {OnConnectionMigration();}
    // Returns the EWMA smoothed RTT for the connection.
    // May return Zero if no valid updates have occurred.
    TimeDelta smoothed_rtt() const { return smoothed_rtt_; }

    // Returns the EWMA smoothed RTT prior to the most recent RTT sample.
    TimeDelta previous_srtt() const { return previous_srtt_; }

    TimeDelta initial_rtt() const { return initial_rtt_; }
    TimeDelta SmoothedOrInitialRtt() const {
    return smoothed_rtt_.IsZero() ? initial_rtt_ : smoothed_rtt_;
  }
    TimeDelta MinOrInitialRtt() const {
    return min_rtt_.IsZero() ? initial_rtt_ : min_rtt_;
   }
    // Sets an initial RTT to be used for SmoothedRtt before any RTT updates.
    void set_initial_rtt(TimeDelta initial_rtt);

    // The most recent rtt measurement.
    // May return Zero if no valid updates have occurred.
    TimeDelta latest_rtt() const { return latest_rtt_; }

    // Returns the min_rtt for the entire connection.
    // May return Zero if no valid updates have occurred.
    TimeDelta min_rtt() const { return min_rtt_; }

    TimeDelta mean_deviation() const { return mean_deviation_; }

    TimeDelta max_ack_delay() const { return max_ack_delay_; }

    ProtoTime last_update_time() const { return last_update_time_; }

    bool ignore_max_ack_delay() const { return ignore_max_ack_delay_; }

    void set_ignore_max_ack_delay(bool ignore_max_ack_delay) {
        ignore_max_ack_delay_ = ignore_max_ack_delay;
    }

    void set_initial_max_ack_delay(TimeDelta initial_max_ack_delay) {
        max_ack_delay_ = std::max(max_ack_delay_, initial_max_ack_delay);
    }
  private:
    TimeDelta latest_rtt_;
    TimeDelta min_rtt_;
    TimeDelta smoothed_rtt_;
    TimeDelta previous_srtt_;
    // Mean RTT deviation during this session.
    // Approximation of standard deviation, the error is roughly 1.25 times
    // larger than the standard deviation, for a normally distributed signal.
    TimeDelta mean_deviation_;
    TimeDelta initial_rtt_;
 // The maximum ack delay observed over the connection after excluding ack
  // delays that were too large to be included in an RTT measurement.
    TimeDelta max_ack_delay_;
    ProtoTime last_update_time_;
  // Whether to ignore the peer's max ack delay.
    bool ignore_max_ack_delay_;
    DISALLOW_COPY_AND_ASSIGN(RttStats);
};
}
