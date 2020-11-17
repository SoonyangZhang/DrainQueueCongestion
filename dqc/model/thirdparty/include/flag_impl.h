#pragma once
#include "proto_types.h"
#include <string>
#define PROTO_FLAG(type,flag,value) extern type flag;
#include "flag_list.h"
#undef PROTO_FLAG
inline bool GetFlagImpl(bool flag) {
  return flag;
}
inline uint64_t GetFlagImpl(uint64_t flag){
    return flag;
}
inline  uint32_t GetFlagImpl(uint32_t flag){
    return flag;
}
inline int64_t GetFlagImpl(int64_t flag){
    return flag;
}
inline  int32_t GetFlagImpl(int32_t flag){
    return flag;
}
inline double GetFlagImpl(double flag) {
  return flag;
}
inline float GetFlagImpl(float flag) {
  return flag;
}
inline std::string GetFlagImpl(const std::string& flag) {
  return flag;
}
#define SetFlagImpl(flag, value) ((flag) = (value))
// ------------------------------------------------------------------------
// QUIC feature flags implementation.
// ------------------------------------------------------------------------
#define RELOADABLE_FLAG(flag) FLAGS_quic_reloadable_flag_##flag
#define RESTART_FLAG(flag) FLAGS_quic_restart_flag_##flag

#define GetQuicFlag(flag) GetFlagImpl(flag)
#define SetQuicFlag(flag,value) SetFlagImpl(flag,value)

#define GetQuicReloadableFlagImpl(flag) GetFlagImpl(RELOADABLE_FLAG(flag))
#define SetQuicReloadableFlagImpl(flag, value) \
  SetQuicFlag(RELOADABLE_FLAG(flag), value)
#define GetQuicRestartFlagImpl(flag) GetFlagImpl(RESTART_FLAG(flag))
#define SetQuicRestartFlagImpl(flag, value) \
  SetQuicFlag(RESTART_FLAG(flag), value)

#define GetQuicReloadableFlag(flag) GetQuicReloadableFlagImpl(flag)
#define SetQuicReloadableFlag(flag, value) \
  SetQuicReloadableFlagImpl(flag, value)

