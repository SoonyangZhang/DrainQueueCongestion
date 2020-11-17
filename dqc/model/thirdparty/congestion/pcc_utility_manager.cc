#include <algorithm>
#include <cmath>
#include "pcc_utility_manager.h"
#include "quic_logging.h"

namespace dqc{

namespace {
// Tolerance of loss rate by utility function.
const double kLossTolerance = 0.05f;
// Coefficeint of the loss rate term in utility function.
const double kLossCoefficient = -1000.0f;
// Coefficient of RTT term in utility function.
const double kRTTCoefficient = -200.0f;

}  // namespace

double PccUtilityFunction::CalculateUtility(const MonitorInterval* interval) const{
  // The caller should guarantee utility of this interval is available.
  QUIC_BUG_IF(interval->first_packet_sent_time ==
              interval->last_packet_sent_time);

  // Add the transfer time of the last packet in the monitor interval when
  // calculating monitor interval duration.
  double interval_duration =
      static_cast<double>((interval->last_packet_sent_time -
                              interval->first_packet_sent_time +
                              interval->sending_rate
                                  .TransferTime(kMaxV4PacketSize))
                             .ToMicroseconds());

  double rtt_ratio =
      static_cast<double>(interval->rtt_on_monitor_start.ToMicroseconds()) /
      static_cast<double>(interval->rtt_on_monitor_end.ToMicroseconds());
  if (rtt_ratio > 1.0 - interval->rtt_fluctuation_tolerance_ratio &&
      rtt_ratio < 1.0 + interval->rtt_fluctuation_tolerance_ratio) {
    rtt_ratio = 1.0;
  }
  double latency_penalty =
      1.0 - 1.0 / (1.0 + exp(kRTTCoefficient * (1.0 - rtt_ratio)));

  double bytes_acked = static_cast<double>(interval->bytes_acked);
  double bytes_lost = static_cast<double>(interval->bytes_lost);
  double bytes_sent = static_cast<double>(interval->bytes_sent);
  double loss_rate = bytes_lost / bytes_sent;
  double loss_penalty =
      1.0 - 1.0 / (1.0 + exp(kLossCoefficient * (loss_rate - kLossTolerance)));

  return (bytes_acked / interval_duration * loss_penalty * latency_penalty -
      bytes_lost / interval_duration) * 1000.0;
}
VivaceUtilityFunction::VivaceUtilityFunction(double delay_gradient_coefficient,
                        double loss_coefficient,
                        double throughput_coefficient,
                        double throughput_power,
                        double delay_gradient_threshold,
                        double delay_gradient_negative_bound)
:delay_gradient_coefficient_(delay_gradient_coefficient),
loss_coefficient_(loss_coefficient),
throughput_power_(throughput_power),
throughput_coefficient_(throughput_coefficient),
delay_gradient_threshold_(delay_gradient_threshold),
delay_gradient_negative_bound_(delay_gradient_negative_bound){}


double VivaceUtilityFunction::CalculateUtility(const MonitorInterval* monitor_interval) const{
  double bitrate = monitor_interval->sending_rate.ToBitsPerSecond();
 
  double bytes_lost = static_cast<double>(monitor_interval->bytes_lost);
  double bytes_sent = static_cast<double>(monitor_interval->bytes_sent);
  double loss_rate = bytes_lost / bytes_sent;
  double rtt_gradient =
      monitor_interval->ComputeDelayGradient(delay_gradient_threshold_);
  rtt_gradient = std::max(rtt_gradient, -delay_gradient_negative_bound_);
  return (throughput_coefficient_ * std::pow(bitrate, throughput_power_)) -
         (delay_gradient_coefficient_ * bitrate * rtt_gradient) -
         (loss_coefficient_ * bitrate * loss_rate);
}


ModifiedVivaceUtilityFunction::ModifiedVivaceUtilityFunction(double delay_gradient_coefficient,
                        double loss_coefficient,
                        double throughput_coefficient,
                        double throughput_power,
                        double delay_gradient_threshold,
                        double delay_gradient_negative_bound)
:delay_gradient_coefficient_(delay_gradient_coefficient),
loss_coefficient_(loss_coefficient),
throughput_power_(throughput_power),
throughput_coefficient_(throughput_coefficient),
delay_gradient_threshold_(delay_gradient_threshold),
delay_gradient_negative_bound_(delay_gradient_negative_bound){}


double ModifiedVivaceUtilityFunction::CalculateUtility(const MonitorInterval* monitor_interval) const{
  double bitrate = monitor_interval->sending_rate.ToBitsPerSecond();
 
  double bytes_lost = static_cast<double>(monitor_interval->bytes_lost);
  double bytes_sent = static_cast<double>(monitor_interval->bytes_sent);
  double loss_rate = bytes_lost / bytes_sent;
  double rtt_gradient =
      monitor_interval->ComputeDelayGradient(delay_gradient_threshold_);
  rtt_gradient = std::max(rtt_gradient, -delay_gradient_negative_bound_);

  return (throughput_coefficient_ * std::pow(bitrate, throughput_power_) *
          bitrate) -
         (delay_gradient_coefficient_ * bitrate * bitrate * rtt_gradient) -
         (loss_coefficient_ * bitrate * bitrate * loss_rate);
}
}  // namespace dqc
