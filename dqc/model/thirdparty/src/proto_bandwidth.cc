#include "proto_bandwidth.h"
#include "string_utils.h"
namespace dqc{
std::string QuicBandwidth::ToDebuggingValue() const {
  if (bits_per_second_ < 80000) {
    return ProtoStringPrintf("%" PRId64 " bits/s (%" PRId64 " bytes/s)",
                            bits_per_second_, bits_per_second_ / 8);
  }

  double divisor;
  char unit;
  if (bits_per_second_ < 8 * 1000 * 1000) {
    divisor = 1e3;
    unit = 'k';
  } else if (bits_per_second_ < INT64_C(8) * 1000 * 1000 * 1000) {
    divisor = 1e6;
    unit = 'M';
  } else {
    divisor = 1e9;
    unit = 'G';
  }

  double bits_per_second_with_unit = bits_per_second_ / divisor;
  double bytes_per_second_with_unit = bits_per_second_with_unit / 8;
  return ProtoStringPrintf("%.2f %cbits/s (%.2f %cbytes/s)",
                          bits_per_second_with_unit, unit,
                          bytes_per_second_with_unit, unit);
}
}
