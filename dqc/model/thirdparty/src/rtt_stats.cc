#include "rtt_stats.h"
#include "logging.h"
#include <algorithm>
namespace dqc{
// Default initial rtt used before any samples are received.

const int kInitialRttMs = 100;
const float kAlpha = 0.125f;
const float kOneMinusAlpha = (1 - kAlpha);
const float kBeta = 0.25f;
const float kOneMinusBeta = (1 - kBeta);

RttStats::RttStats()
    : latest_rtt_(TimeDelta::Zero()),
      min_rtt_(TimeDelta::Zero()),
      smoothed_rtt_(TimeDelta::Zero()),
      previous_srtt_(TimeDelta::Zero()),
      mean_deviation_(TimeDelta::Zero()),
      initial_rtt_(TimeDelta::FromMilliseconds(kInitialRttMs)),
      max_ack_delay_(TimeDelta::Zero()),
      last_update_time_(ProtoTime::Zero()),
      ignore_max_ack_delay_(false) {}

void RttStats::set_initial_rtt(TimeDelta initial_rtt)
{
    if (initial_rtt.ToMicroseconds() <= 0)
    {
        DLOG(WARNING)<<"Attempt to set initial rtt to <= 0.";
        return;
    }
    initial_rtt_= initial_rtt;
}

void RttStats::ExpireSmoothedMetrics()
{
  mean_deviation_ = std::max(
      mean_deviation_, TimeDelta::FromMicroseconds(std::abs(
                           (smoothed_rtt_ - latest_rtt_).ToMicroseconds())));
  smoothed_rtt_ = std::max(smoothed_rtt_, latest_rtt_);
}

// Updates the RTT based on a new sample.
void RttStats::UpdateRtt(TimeDelta send_delta,
                         TimeDelta ack_delay,
                         ProtoTime now)
{
  if (send_delta.IsInfinite() || send_delta <= TimeDelta::Zero()) {
        DLOG(WARNING)<< "Ignoring measured send_delta, because it's is "
        << "either infinite, zero, or negative.  send_delta = "
        << send_delta.ToMicroseconds();
    return;
  }
  last_update_time_ = now;
  // Update min_rtt_ first. min_rtt_ does not use an rtt_sample corrected for
  // ack_delay but the raw observed send_delta, since poor clock granularity at
  // the client may cause a high ack_delay to result in underestimation of the
  // min_rtt_.
  if (min_rtt_.IsZero() || min_rtt_ > send_delta) {
    min_rtt_ = send_delta;
  }

  TimeDelta rtt_sample(send_delta);
  previous_srtt_ = smoothed_rtt_;

  if (ignore_max_ack_delay_) {
    ack_delay = TimeDelta::Zero();
  }
  // Correct for ack_delay if information received from the peer results in a
  // an RTT sample at least as large as min_rtt. Otherwise, only use the
  // send_delta.
  if (rtt_sample > ack_delay) {
    if (rtt_sample - min_rtt_ >= ack_delay) {
      max_ack_delay_ = std::max(max_ack_delay_, ack_delay);
      rtt_sample = rtt_sample - ack_delay;
    }
  }
  latest_rtt_ = rtt_sample;
  // First time call.
  if (smoothed_rtt_.IsZero()) {
    smoothed_rtt_ = rtt_sample;
    mean_deviation_ =
        TimeDelta::FromMicroseconds(rtt_sample.ToMicroseconds() / 2);
  } else {
    mean_deviation_ = TimeDelta::FromMicroseconds(static_cast<int64_t>(
        kOneMinusBeta * mean_deviation_.ToMicroseconds() +
        kBeta * std::abs((smoothed_rtt_ - rtt_sample).ToMicroseconds())));
    smoothed_rtt_ = kOneMinusAlpha * smoothed_rtt_ + kAlpha * rtt_sample;
    DLOG(INFO)<< " smoothed_rtt(us):" << smoothed_rtt_.ToMicroseconds()
                  << " mean_deviation(us):" << mean_deviation_.ToMicroseconds();
  }
}

void RttStats::OnConnectionMigration()
{
  latest_rtt_ = TimeDelta::Zero();
  min_rtt_ = TimeDelta::Zero();
  smoothed_rtt_ = TimeDelta::Zero();
  mean_deviation_ =TimeDelta::Zero();
  initial_rtt_ = TimeDelta::FromMilliseconds(kInitialRttMs);
  max_ack_delay_=TimeDelta::Zero();
}
}
