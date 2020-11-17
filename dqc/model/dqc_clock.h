#pragma once
#include "ns3/proto_time.h"
#include "ns3/simulator.h"
namespace ns3{
class DqcSimuClock:public dqc::ProtoClock{
public:
    dqc::ProtoTime Now() const override{
        int64_t ms=Simulator::Now().GetMilliSeconds();
        dqc::ProtoTime current_ts=dqc::ProtoTime::Zero()+dqc::TimeDelta::FromMilliseconds(ms);
        return current_ts;
    }
    dqc::ProtoTime ApproximateNow() const override{
        return Now();
    }   
};   
}
