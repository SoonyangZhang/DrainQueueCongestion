#pragma once
#include <limits>
#include <cstdint>
#include <ostream>
#include "logging.h"
namespace dqc{
// QuicPacketNumber can either initialized or uninitialized. An initialized
// packet number is simply an ordinal number. A sentinel value is used to
// represent an uninitialized packet number.
class  QuicPacketNumber {
 public:
  // Construct an uninitialized packet number.
  QuicPacketNumber();
  // Construct a packet number from uint64_t. |packet_number| cannot equal the
  // sentinel value.
  explicit QuicPacketNumber(uint64_t packet_number);

  // Packet number becomes uninitialized after calling this function.
  void Clear();

  // Updates this packet number to be |new_value| if it is greater than current
  // value.
  void UpdateMax(QuicPacketNumber new_value);

  // REQUIRES: IsInitialized() == true.
  uint64_t Hash() const;

  // Converts packet number to uint64_t.
  // REQUIRES: IsInitialized() == true.
  uint64_t ToUint64() const;

  // Returns true if packet number is considered initialized.
  bool IsInitialized() const;

  // REQUIRES: IsInitialized() == true && ToUint64() <
  // numeric_limits<uint64_t>::max() - 1.
  QuicPacketNumber& operator++();
  QuicPacketNumber operator++(int);
  // REQUIRES: IsInitialized() == true && ToUint64() >= 1.
  QuicPacketNumber& operator--();
  QuicPacketNumber operator--(int);

  // REQUIRES: IsInitialized() == true && numeric_limits<uint64_t>::max() -
  // ToUint64() > |delta|.
  QuicPacketNumber& operator+=(uint64_t delta);
  // REQUIRES: IsInitialized() == true && ToUint64() >= |delta|.
  QuicPacketNumber& operator-=(uint64_t delta);

  friend std::ostream& operator<<(
      std::ostream& os,
      const QuicPacketNumber& p);

 private:
  // All following operators REQUIRE operands.Initialized() == true.
  friend inline bool operator==(QuicPacketNumber lhs, QuicPacketNumber rhs);
  friend inline bool operator!=(QuicPacketNumber lhs, QuicPacketNumber rhs);
  friend inline bool operator<(QuicPacketNumber lhs, QuicPacketNumber rhs);
  friend inline bool operator<=(QuicPacketNumber lhs, QuicPacketNumber rhs);
  friend inline bool operator>(QuicPacketNumber lhs, QuicPacketNumber rhs);
  friend inline bool operator>=(QuicPacketNumber lhs, QuicPacketNumber rhs);

  // REQUIRES: numeric_limits<uint64_t>::max() - lhs.ToUint64() > |delta|.
  friend inline QuicPacketNumber operator+(QuicPacketNumber lhs,
                                           uint64_t delta);
  // REQUIRES: lhs.ToUint64() >= |delta|.
  friend inline QuicPacketNumber operator-(QuicPacketNumber lhs,
                                           uint64_t delta);
  // REQUIRES: lhs >= rhs.
  friend inline uint64_t operator-(QuicPacketNumber lhs, QuicPacketNumber rhs);

  // The sentinel value representing an uninitialized packet number.
  static uint64_t UninitializedPacketNumber();

  uint64_t packet_number_;
};

class QuicPacketNumberHash {
 public:
  uint64_t operator()(QuicPacketNumber packet_number) const noexcept {
    return packet_number.Hash();
  }
};

inline bool operator==(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized()) << lhs << " vs. " << rhs;
  return lhs.packet_number_ == rhs.packet_number_;
}

inline bool operator!=(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized()) << lhs << " vs. " << rhs;
  return lhs.packet_number_ != rhs.packet_number_;
}

inline bool operator<(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized()) << lhs << " vs. " << rhs;
  return lhs.packet_number_ < rhs.packet_number_;
}

inline bool operator<=(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized()) << lhs << " vs. " << rhs;
  return lhs.packet_number_ <= rhs.packet_number_;
}

inline bool operator>(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized()) << lhs << " vs. " << rhs;
  return lhs.packet_number_ > rhs.packet_number_;
}

inline bool operator>=(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized()) << lhs << " vs. " << rhs;
  return lhs.packet_number_ >= rhs.packet_number_;
}

inline QuicPacketNumber operator+(QuicPacketNumber lhs, uint64_t delta) {
#ifndef NDEBUG
  DCHECK(lhs.IsInitialized());
  DCHECK_GT(std::numeric_limits<uint64_t>::max() - lhs.ToUint64(), delta);
#endif
  return QuicPacketNumber(lhs.packet_number_ + delta);
}

inline QuicPacketNumber operator-(QuicPacketNumber lhs, uint64_t delta) {
#ifndef NDEBUG
  DCHECK(lhs.IsInitialized());
  DCHECK_GE(lhs.ToUint64(), delta);
#endif
  return QuicPacketNumber(lhs.packet_number_ - delta);
}

inline uint64_t operator-(QuicPacketNumber lhs, QuicPacketNumber rhs) {
  DCHECK(lhs.IsInitialized() && rhs.IsInitialized() && lhs >= rhs)
      << lhs << " vs. " << rhs;
  return lhs.packet_number_ - rhs.packet_number_;
}
}
