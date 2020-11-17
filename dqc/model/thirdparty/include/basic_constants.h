#pragma once
#include <stdint.h>
namespace basic{
// We define an unsigned 16-bit floating point value, inspired by IEEE floats
// (http://en.wikipedia.org/wiki/Half_precision_floating-point_format),
// with 5-bit exponent (bias 1), 11-bit mantissa (effective 12 with hidden
// bit) and denormals, but without signs, transfinites or fractions. Wire format
// 16 bits (little-endian byte order) are split into exponent (high 5) and
// mantissa (low 11) and decoded as:
//   uint64_t value;
//   if (exponent == 0) value = mantissa;
//   else value = (mantissa | 1 << 11) << (exponent - 1)
const int kUFloat16ExponentBits = 5;
const int kUFloat16MaxExponent = (1 << kUFloat16ExponentBits) - 2;     // 30
const int kUFloat16MantissaBits = 16 - kUFloat16ExponentBits;          // 11
const int kUFloat16MantissaEffectiveBits = kUFloat16MantissaBits + 1;  // 12
const uint64_t kUFloat16MaxValue =  // 0x3FFC0000000
    ((UINT64_C(1) << kUFloat16MantissaEffectiveBits) - 1)
    << kUFloat16MaxExponent;
}
