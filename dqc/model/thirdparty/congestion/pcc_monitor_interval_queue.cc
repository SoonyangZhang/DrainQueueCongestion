#include "pcc_monitor_interval_queue.h"
#include "rtt_stats.h"
#include "quic_logging.h"
namespace dqc {

MonitorInterval::MonitorInterval()
    : sending_rate(QuicBandwidth::Zero()),
      is_useful(false),
      rtt_fluctuation_tolerance_ratio(0.0),
      first_packet_sent_time(ProtoTime::Zero()),
      last_packet_sent_time(ProtoTime::Zero()),
      first_packet_number(0),
      last_packet_number(0),
      bytes_sent(0),
      bytes_acked(0),
      bytes_lost(0),
      rtt_on_monitor_start(TimeDelta::Zero()),
      rtt_on_monitor_end(TimeDelta::Zero()) {}

MonitorInterval::MonitorInterval(QuicBandwidth sending_rate,
                                 bool is_useful,
                                 float rtt_fluctuation_tolerance_ratio,
                                 TimeDelta rtt)
    : sending_rate(sending_rate),
      is_useful(is_useful),
      rtt_fluctuation_tolerance_ratio(rtt_fluctuation_tolerance_ratio),
      first_packet_sent_time(ProtoTime::Zero()),
      last_packet_sent_time(ProtoTime::Zero()),
      first_packet_number(0),
      last_packet_number(0),
      bytes_sent(0),
      bytes_acked(0),
      bytes_lost(0),
      rtt_on_monitor_start(rtt),
      rtt_on_monitor_end(rtt) {}
void MonitorInterval::OnNewDelaySample(const ReceivedPacket &sample){
    if(sample.delay.IsZero()){
        return;
    }
    if(received_packets_.empty()){
        received_packets_.push_back(sample);
        return ;
    }
    if(received_packets_.back().packet_number<sample.packet_number){
        received_packets_.push_back(sample);
    }
}
double MonitorInterval::ComputeDelayGradient(double delay_gradient_threshold) const{
    if (received_packets_.empty() || received_packets_.front().sent_time ==
                                       received_packets_.back().sent_time) {
        return 0;
    }
  double sum_times = 0;
  double sum_delays = 0;
  for (const ReceivedPacket& packet : received_packets_) {
    double time_delta_us =
        (packet.sent_time - received_packets_[0].sent_time).ToMicroseconds();
    double delay = packet.delay.ToMicroseconds();
    sum_times += time_delta_us;
    sum_delays += delay;
  }
  
  
  double sum_squared_scaled_time_deltas = 0;
  double sum_scaled_time_delta_dot_delay = 0;
  for (const ReceivedPacket& packet : received_packets_) {
    double time_delta_us =
        (packet.sent_time - received_packets_[0].sent_time).ToMicroseconds();
    double delay = packet.delay.ToMicroseconds();
    double scaled_time_delta_us =
        time_delta_us - sum_times / received_packets_.size();
    sum_squared_scaled_time_deltas +=
        scaled_time_delta_us * scaled_time_delta_us;
    sum_scaled_time_delta_dot_delay += scaled_time_delta_us * delay;
  }
  double rtt_gradient =
      sum_scaled_time_delta_dot_delay / sum_squared_scaled_time_deltas;
  if (std::abs(rtt_gradient) < delay_gradient_threshold)
    rtt_gradient = 0;
  return rtt_gradient;
}
PccMonitorIntervalQueue::PccMonitorIntervalQueue(const RttStats* rtt_stats,
    PccMonitorIntervalQueueDelegateInterface* delegate)
    :rtt_stats_(rtt_stats),
    num_useful_intervals_(0),
    num_available_intervals_(0),
    delegate_(delegate) {}

void PccMonitorIntervalQueue::EnqueueNewMonitorInterval(
    QuicBandwidth sending_rate,
    bool is_useful,
    float rtt_fluctuation_tolerance_ratio) {
  if (is_useful) {
    ++num_useful_intervals_;
  }

  monitor_intervals_.emplace_back(sending_rate, is_useful,
                                  rtt_fluctuation_tolerance_ratio,rtt_stats_->smoothed_rtt());
}

void PccMonitorIntervalQueue::OnPacketSent(ProtoTime sent_time,
                                           QuicPacketNumber packet_number,
                                           QuicByteCount bytes) {
  if (monitor_intervals_.empty()) {
    QUIC_BUG << "OnPacketSent called with empty queue.";
    return;
  }

  if (monitor_intervals_.back().bytes_sent == 0) {
    // This is the first packet of this interval.
    monitor_intervals_.back().first_packet_sent_time = sent_time;
    monitor_intervals_.back().first_packet_number = packet_number;
  }

  monitor_intervals_.back().last_packet_sent_time = sent_time;
  monitor_intervals_.back().last_packet_number = packet_number;
  monitor_intervals_.back().bytes_sent += bytes;
}

void PccMonitorIntervalQueue::OnCongestionEvent(
    const ReceivedPacketVector& acked_packets,
    const LostPacketVector& lost_packets){
  TimeDelta rtt=rtt_stats_->smoothed_rtt();
  QUIC_BUG_IF(rtt.IsZero());
  num_available_intervals_ = 0;
  if (num_useful_intervals_ == 0) {
    // Skip all the received packets if no intervals are useful.
    return;
  }

  bool has_invalid_utility = false;
  for (MonitorInterval& interval : monitor_intervals_) {
    if (!interval.is_useful) {
      // Skips useless monitor intervals.
      continue;
    }

    if (IsUtilityAvailable(interval)) {
      // Skip intervals with available utilities.
      ++num_available_intervals_;
      continue;
    }

    for (const LostPacket& lost_packet : lost_packets) {
      if (IntervalContainsPacket(interval, lost_packet.packet_number)) {
        interval.bytes_lost += lost_packet.bytes_lost;
      }
    }

    for (const ReceivedPacket& acked_packet : acked_packets) {
      if (IntervalContainsPacket(interval, acked_packet.packet_number)) {
        if (interval.bytes_acked == 0) {
          // This is the RTT before starting sending at interval.sending_rate.
          interval.rtt_on_monitor_start = rtt;
        }
        interval.bytes_acked += acked_packet.bytes_acked;
        interval.OnNewDelaySample(acked_packet);
      }
    }

    if (IsUtilityAvailable(interval)) {
      interval.rtt_on_monitor_end = rtt;
      has_invalid_utility = HasInvalidUtility(&interval);
      if (has_invalid_utility) {
        break;
      }
      ++num_available_intervals_;
      QUIC_BUG_IF(num_available_intervals_ > num_useful_intervals_);
    }
  }

  if (num_useful_intervals_ > num_available_intervals_ &&
      !has_invalid_utility) {
    return;
  }

  if (!has_invalid_utility) {
    DCHECK_GT(num_useful_intervals_, 0u);

    std::vector<const MonitorInterval *> useful_intervals;
    for (const MonitorInterval& interval : monitor_intervals_) {
      if (!interval.is_useful) {
        continue;
      }
      useful_intervals.push_back(&interval);
    }
    DCHECK_EQ(num_available_intervals_, useful_intervals.size());

    delegate_->OnUtilityAvailable(useful_intervals);
  }

  // Remove MonitorIntervals from the head of the queue,
  // until all useful intervals are removed.
  while (num_useful_intervals_ > 0) {
    if (monitor_intervals_.front().is_useful) {
      --num_useful_intervals_;
    }
    monitor_intervals_.pop_front();
  }
  num_available_intervals_ = 0;
}

const MonitorInterval& PccMonitorIntervalQueue::front() const {
  DCHECK(!monitor_intervals_.empty());
  return monitor_intervals_.front();
}

const MonitorInterval& PccMonitorIntervalQueue::current() const {
  DCHECK(!monitor_intervals_.empty());
  return monitor_intervals_.back();
}

bool PccMonitorIntervalQueue::empty() const {
  return monitor_intervals_.empty();
}

size_t PccMonitorIntervalQueue::size() const {
  return monitor_intervals_.size();
}

void PccMonitorIntervalQueue::OnRttInflationInStarting() {
  monitor_intervals_.clear();
  num_useful_intervals_ = 0;
  num_available_intervals_ = 0;
}

bool PccMonitorIntervalQueue::IsUtilityAvailable(
    const MonitorInterval& interval) const {
  return (interval.bytes_acked + interval.bytes_lost == interval.bytes_sent);
}

bool PccMonitorIntervalQueue::IntervalContainsPacket(
    const MonitorInterval& interval,
    QuicPacketNumber packet_number) const {
  return (packet_number >= interval.first_packet_number &&
          packet_number <= interval.last_packet_number);
}

bool PccMonitorIntervalQueue::HasInvalidUtility(
    const MonitorInterval* interval) const {
  return interval->first_packet_sent_time == interval->last_packet_sent_time;
}

}  // namespace dqc