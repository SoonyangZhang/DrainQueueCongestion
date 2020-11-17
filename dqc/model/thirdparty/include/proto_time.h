#pragma once
#include <stdint.h>
#include <cstddef>
#include <limits>
#include <cmath>
#include <string>
#include <ostream>
namespace dqc{
class ProtoTime;
class TimeDelta{
public:
    static constexpr TimeDelta Zero(){
        return TimeDelta(0);
    }
    static constexpr TimeDelta Infinite(){
        return TimeDelta(kInfiniteTimeUs);
    }
    static constexpr TimeDelta FromSeconds(int64_t secs){
        return TimeDelta(secs*1000*1000);
    }
    static constexpr TimeDelta FromMilliseconds(int64_t ms){
        return TimeDelta(ms*1000);
    }
    static constexpr TimeDelta FromMicroseconds(int64_t us){
        return TimeDelta(us);
    }
    inline int64_t ToSeconds() const{
        return time_offset_/1000/1000;
    }
    inline int64_t ToMilliseconds() const{
        return time_offset_/1000;
    }
    inline int64_t ToMicroseconds() const{
        return time_offset_;
    }
    inline bool IsZero() const { return time_offset_ == 0; }

    inline bool IsInfinite() const {
      return time_offset_ == kInfiniteTimeUs;
    }
    std::string ToDebuggingValue() const;
private:
    friend inline bool operator==(TimeDelta lhs, TimeDelta rhs);
    friend inline bool operator!=(TimeDelta lhs, TimeDelta rhs);
    friend inline bool operator<(TimeDelta lhs, TimeDelta rhs);
    friend inline bool operator<=(TimeDelta lhs, TimeDelta rhs);
    friend inline bool operator>(TimeDelta lhs, TimeDelta rhs);
    friend inline bool operator>=(TimeDelta lhs, TimeDelta rhs);
    friend inline TimeDelta operator>>(TimeDelta lhs, size_t rhs);
    friend inline TimeDelta operator+(TimeDelta lhs, TimeDelta rhs);
    friend inline TimeDelta operator-(TimeDelta lhs, TimeDelta rhs);
    friend inline TimeDelta operator*(TimeDelta lhs, int rhs);
    friend inline TimeDelta operator*(TimeDelta lhs, double rhs);

    friend inline ProtoTime operator+(ProtoTime lhs, TimeDelta rhs);
    friend inline ProtoTime operator-(ProtoTime lhs, TimeDelta rhs);
    friend inline TimeDelta operator-(ProtoTime lhs, ProtoTime rhs);


    static const int64_t kInfiniteTimeUs =
    std::numeric_limits<int64_t>::max();
    explicit constexpr TimeDelta(int64_t time_offset)
    :time_offset_(time_offset){
    }
    int64_t time_offset_;
    friend class ProtoTime;
};
class ProtoTime{
public:
  // Creates a new ProtoTime with an internal value of 0.  IsInitialized()
  // will return false for these times.
  static constexpr ProtoTime Zero() { return ProtoTime(0); }

  // Creates a new ProtoTime with an infinite time.
  static constexpr ProtoTime Infinite() {
    return ProtoTime(TimeDelta::kInfiniteTimeUs);
  }

  ProtoTime(const ProtoTime& other) = default;

  ProtoTime& operator=(const ProtoTime& other) {
    time_ = other.time_;
    return *this;
  }

  // Produce the internal value to be used when logging.  This value
  // represents the number of microseconds since some epoch.  It may
  // be the UNIX epoch on some platforms.  On others, it may
  // be a CPU ticks based value.
  inline int64_t ToDebuggingValue() const { return time_; }

  inline bool IsInitialized() const { return 0 != time_; }
private:
    friend inline bool operator==(ProtoTime lhs, ProtoTime rhs);
    friend inline bool operator<(ProtoTime lhs, ProtoTime rhs);
    friend inline ProtoTime operator+(ProtoTime lhs, TimeDelta rhs);
    friend inline ProtoTime operator-(ProtoTime lhs, TimeDelta rhs);
    friend inline TimeDelta operator-(ProtoTime lhs, ProtoTime rhs);
    explicit constexpr ProtoTime(int64_t time) : time_(time) {}
    int64_t time_{0};
};
inline bool operator==(TimeDelta lhs, TimeDelta rhs){
    return lhs.time_offset_==rhs.time_offset_;
}
inline bool operator!=(TimeDelta lhs, TimeDelta rhs) {
  return !(lhs == rhs);
}
inline bool operator<(TimeDelta lhs, TimeDelta rhs) {
  return lhs.time_offset_ < rhs.time_offset_;
}
inline bool operator>(TimeDelta lhs, TimeDelta rhs) {
  return rhs < lhs;
}
inline bool operator<=(TimeDelta lhs, TimeDelta rhs) {
  return !(rhs < lhs);
}
inline bool operator>=(TimeDelta lhs, TimeDelta rhs) {
  return !(lhs < rhs);
}
inline TimeDelta operator>>(TimeDelta lhs, size_t rhs) {
  return TimeDelta(lhs.time_offset_ >> rhs);
}


// Non-member relational operators for ProtoTime.
inline bool operator==(ProtoTime lhs, ProtoTime rhs) {
  return lhs.time_ == rhs.time_;
}
inline bool operator!=(ProtoTime lhs, ProtoTime rhs) {
  return !(lhs == rhs);
}
inline bool operator<(ProtoTime lhs, ProtoTime rhs) {
  return lhs.time_ < rhs.time_;
}
inline bool operator>(ProtoTime lhs, ProtoTime rhs) {
  return rhs < lhs;
}
inline bool operator<=(ProtoTime lhs, ProtoTime rhs) {
  return !(rhs < lhs);
}
inline bool operator>=(ProtoTime lhs, ProtoTime rhs) {
  return !(lhs < rhs);
}
// Override stream output operator for gtest or CHECK macros.
inline std::ostream& operator<<(std::ostream& output, const ProtoTime t) {
  output << t.ToDebuggingValue();
  return output;
}
// Non-member arithmetic operators for TimeDelta.
inline TimeDelta operator+(TimeDelta lhs, TimeDelta rhs) {
  return TimeDelta(lhs.time_offset_ + rhs.time_offset_);
}
inline TimeDelta operator-(TimeDelta lhs, TimeDelta rhs) {
  return TimeDelta(lhs.time_offset_ - rhs.time_offset_);
}
inline TimeDelta operator*(TimeDelta lhs, int rhs) {
  return TimeDelta(lhs.time_offset_ * rhs);
}
inline TimeDelta operator*(TimeDelta lhs, double rhs) {
  return TimeDelta(
      static_cast<int64_t>(std::llround(lhs.time_offset_ * rhs)));
}
inline TimeDelta operator*(int lhs, TimeDelta rhs) {
  return rhs * lhs;
}
inline TimeDelta operator*(double lhs, TimeDelta rhs) {
  return rhs * lhs;
}
// Override stream output operator for gtest.
inline std::ostream& operator<<(std::ostream& output,
                                const TimeDelta delta) {
  output << delta.ToDebuggingValue();
  return output;
}
// Non-member arithmetic operators for ProtoTime and TimeDelta.
inline ProtoTime operator+(ProtoTime lhs, TimeDelta rhs) {
  return ProtoTime(lhs.time_ + rhs.time_offset_);
}
inline ProtoTime operator-(ProtoTime lhs, TimeDelta rhs) {
  return ProtoTime(lhs.time_ - rhs.time_offset_);
}
inline TimeDelta operator-(ProtoTime lhs, ProtoTime rhs) {
  return TimeDelta(lhs.time_ - rhs.time_);
}
class ProtoClock{
public:
    virtual ~ProtoClock(){}
    virtual ProtoTime Now() const=0;
    virtual ProtoTime ApproximateNow()const=0;
};
class SystemClock: public ProtoClock{
public:
    SystemClock(){}
    ~SystemClock(){}
    ProtoTime Now() const override;
    ProtoTime ApproximateNow() const override;
};
void SetClockForTesting(ProtoClock *clock);
int64_t TimeMillis();
int64_t TimeMicro();
void TimeSleep(int64_t milliseconds);
}
