#pragma once
#include <string>
#include <sstream>
namespace dqc{
template <typename... Args>
inline std::string QuicStrCatImpl(const Args&... args) {
  std::ostringstream oss;
  int dummy[] = {1, (oss << args, 0)...};
  static_cast<void>(dummy);
  return oss.str();
}
// Merges given strings or numbers with no delimiter.
template <typename... Args>
inline std::string QuicStrCat(const Args&... args) {
  return QuicStrCatImpl(std::forward<const Args&>(args)...);
}
std::string ProtoStringPrintf(const char *format,...);
}
