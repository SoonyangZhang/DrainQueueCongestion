#pragma once
#include "proto_types.h"
namespace dqc{
class PrrSender {
 public:
  PrrSender();
  // OnPacketLost should be called on the first loss that triggers a recovery
  // period and all other methods in this class should only be called when in
  // recovery.
  void OnPacketLost(QuicByteCount prior_in_flight);
  void OnPacketSent(QuicByteCount sent_bytes);
  void OnPacketAcked(QuicByteCount acked_bytes);
  bool CanSend(QuicByteCount congestion_window,
               QuicByteCount bytes_in_flight,
               QuicByteCount slowstart_threshold) const;

 private:
  // Bytes sent and acked since the last loss event.
  // |bytes_sent_since_loss_| is the same as "prr_out_" in RFC 6937,
  // and |bytes_delivered_since_loss_| is the same as "prr_delivered_".
  QuicByteCount bytes_sent_since_loss_;
  QuicByteCount bytes_delivered_since_loss_;
  size_t ack_count_since_loss_;

  // The congestion window before the last loss event.
  QuicByteCount bytes_in_flight_before_loss_;
};
}
