#pragma once

#include <deque>
#include <utility>
#include <vector>

#include "proto_send_algorithm_interface.h"
#include "proto_time.h"
#include "proto_types.h"

namespace dqc {
class RttStats;
// MonitorInterval, as the queue's entry struct, stores the information
// of a PCC monitor interval (MonitorInterval) that can be used to
// - pinpoint a acked/lost packet to the corresponding MonitorInterval,
// - calculate the MonitorInterval's utility value.
struct ReceivedPacket{
  ReceivedPacket(QuicPacketNumber seq,PacketLength bytes):ReceivedPacket(ProtoTime::Zero(),seq,TimeDelta::Zero(),bytes){}
  ReceivedPacket(ProtoTime event_time,QuicPacketNumber seq,TimeDelta rtt,PacketLength bytes):
  sent_time(event_time),packet_number(seq),delay(rtt),bytes_acked(bytes){}
  ProtoTime sent_time;
  QuicPacketNumber packet_number;
  TimeDelta delay;
  PacketLength bytes_acked;
};
typedef std::vector<ReceivedPacket> ReceivedPacketVector;
struct MonitorInterval {
  MonitorInterval();
  MonitorInterval(QuicBandwidth sending_rate,
                  bool is_useful,
                  float rtt_fluctuation_tolerance_ratio,
                  TimeDelta rtt);
  ~MonitorInterval() {
      received_packets_.clear();
  }
  void OnNewDelaySample(const ReceivedPacket &sample);
  double ComputeDelayGradient(double delay_gradient_threshold) const;
  // Sending rate.
  QuicBandwidth sending_rate;
  // True if calculating utility for this MonitorInterval.
  bool is_useful;
  // The tolerable rtt fluctuation ratio.
  float rtt_fluctuation_tolerance_ratio;

  // Sent time of the first packet.
  ProtoTime first_packet_sent_time;
  // Sent time of the last packet.
  ProtoTime last_packet_sent_time;

  // PacketNumber of the first sent packet.
  QuicPacketNumber first_packet_number;
  // PacketNumber of the last sent packet.
  QuicPacketNumber last_packet_number;

  // Number of bytes which are sent in total.
  QuicByteCount bytes_sent;
  // Number of bytes which have been acked.
  QuicByteCount bytes_acked;
  // Number of bytes which are considered as lost.
  QuicByteCount bytes_lost;

  // Smoothed RTT when the first packet is sent.
  TimeDelta rtt_on_monitor_start;
  // RTT when all sent packets are either acked or lost.
  TimeDelta rtt_on_monitor_end;
  ReceivedPacketVector received_packets_;
};

// A delegate interface for further processing when all
// 'useful' MonitorIntervals' utilities are available.
class  PccMonitorIntervalQueueDelegateInterface {
 public:
  virtual ~PccMonitorIntervalQueueDelegateInterface() {}

  virtual void OnUtilityAvailable(
      const std::vector<const MonitorInterval *>& useful_intervals) = 0;
};

// PccMonitorIntervalQueue contains a queue of MonitorIntervals.
// New MonitorIntervals are added to the tail of the queue.
// Existing MonitorIntervals are removed from the queue when all
// 'useful' intervals' utilities are available.
class PccMonitorIntervalQueue {
 public:
  explicit PccMonitorIntervalQueue(const RttStats* rtt_stats,
      PccMonitorIntervalQueueDelegateInterface* delegate);
  PccMonitorIntervalQueue(const PccMonitorIntervalQueue&) = delete;
  PccMonitorIntervalQueue& operator=(const PccMonitorIntervalQueue&) = delete;
  PccMonitorIntervalQueue(PccMonitorIntervalQueue&&) = delete;
  PccMonitorIntervalQueue& operator=(PccMonitorIntervalQueue&&) = delete;
  ~PccMonitorIntervalQueue() {}

  // Creates a new MonitorInterval and add it to the tail of the
  // monitor interval queue, provided the necessary variables
  // for MonitorInterval initialization.
  void EnqueueNewMonitorInterval(QuicBandwidth sending_rate,
                                 bool is_useful,
                                 float rtt_fluctuation_tolerance_ratio);

  // Called when a packet belonging to current monitor interval is sent.
  void OnPacketSent(ProtoTime sent_time,
                    QuicPacketNumber packet_number,
                    QuicByteCount bytes);

  // Called when packets are acked or considered as lost.
  void OnCongestionEvent(const ReceivedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets);

  // Called when RTT inflation ratio is greater than
  // max_rtt_fluctuation_tolerance_ratio_in_starting.
  void OnRttInflationInStarting();

  // Returns the fisrt MonitorInterval in the front of the queue. The caller
  // needs to make sure the queue is not empty before calling this function.
  const MonitorInterval& front() const;
  // Returns the most recent MonitorInterval in the tail of the queue. The
  // caller needs to make sure the queue is not empty before calling this
  // function.
  const MonitorInterval& current() const;
  size_t num_useful_intervals() const { return num_useful_intervals_; }
  size_t num_available_intervals() const { return num_available_intervals_; }
  bool empty() const;
  size_t size() const;

 private:
  // Returns true if the utility of |interval| is available, i.e.,
  // when all the interval's packets are either acked or lost.
  bool IsUtilityAvailable(const MonitorInterval& interval) const;

  // Retruns true if |packet_number| belongs to |interval|.
  bool IntervalContainsPacket(const MonitorInterval& interval,
                              QuicPacketNumber packet_number) const;

  // Returns true if the utility of |interval| is invalid, i.e., if it only
  // contains a single sent packet.
  bool HasInvalidUtility(const MonitorInterval* interval) const;
  const RttStats* rtt_stats_;
  std::deque<MonitorInterval> monitor_intervals_;
  // Number of useful intervals in the queue.
  size_t num_useful_intervals_;
  // Number of useful intervals in the queue with available utilities.
  size_t num_available_intervals_;
  // Delegate interface, not owned.
  PccMonitorIntervalQueueDelegateInterface* delegate_;
};

}  // namespace dqc
