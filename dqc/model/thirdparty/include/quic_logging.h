#pragma once
#include "logging.h"
#define QUIC_DLOG(severity) DLOG(severity)
#define QUIC_LOG_IF(severity, condition) DLOG_IF(severity, condition)
#define QUIC_BUG  LOG(FATAL)
#define QUIC_BUG_IF(condition) LOG_IF(FATAL, condition)
#define QUIC_DVLOG(x)  DLOG(INFO)

#define QUIC_LOG_FIRST_N(severity, n) LOG_EVERY_N(severity,n)
#define QUIC_CODE_COUNT_IMPL(name) \
  do {                             \
  } while (0)
#define QUIC_CODE_COUNT_N_IMPL(name, instance, total) \
  do {                                                \
  } while (0)
      

#define QUIC_CODE_COUNT QUIC_CODE_COUNT_IMPL
#define QUIC_CODE_COUNT_N QUIC_CODE_COUNT_N_IMPL

#define NOTREACHED() DCHECK(false)
#define QUIC_NOTREACHED() NOTREACHED()
