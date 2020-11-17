#pragma once
#include<stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NS3_SIMULATION_CLOCK
#if defined NS3_SIMULATION_CLOCK
int64_t get_ns_clock_ms();
#endif

#ifdef __cplusplus
}
#endif

