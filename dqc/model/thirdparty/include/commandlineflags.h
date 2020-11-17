#pragma once 
#define DEFINE_VARIABLE(namespc, type, name, value, help)                      \
  namespace Flag_Names_##type {                                                \
    namespc type FLAGS_##name =                                               \
      value;                      \
  }                                                                            \
  using Flag_Names_##type::FLAGS_##name

#define DEFINE_bool(name, val, txt)    DEFINE_VARIABLE(, bool, name, val, txt)
#define DEFINE_int32(name, val, txt)   DEFINE_VARIABLE(, int32_t, name, val, txt)
#define DEFINE_int64(name, val, txt)   DEFINE_VARIABLE(, int64_t, name, val, txt)
#define DEFINE_uint64(name, val, txt)  DEFINE_VARIABLE(, uint64_t, name, val, txt)
#define DEFINE_double(name, val, txt)  DEFINE_VARIABLE(, double, name, val, txt)
#define DEFINE_string(name, val, txt)  DEFINE_VARIABLE(, std::string, name, val, txt)