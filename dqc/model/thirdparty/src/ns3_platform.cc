#include "ns3_platform.h"
#if defined NS3_SIMULATION_CLOCK
#include "ns3/simulator.h"
using namespace ns3;
int64_t get_ns_clock_ms(){
	return Simulator::Now ().GetMilliSeconds();
}
#endif
