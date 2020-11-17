#include "rtt_monitor.h"
namespace dqc{
TimeDelta RttMonitor::Average() const{
    TimeDelta average=TimeDelta::Zero();
    int num=Size();
    if(num>0){
        double all=sum_rtt_;
        int64_t div=all/num;
        average=TimeDelta::FromMilliseconds(div);
    }
    return average;
}
void RttMonitor::Update(ProtoTime ack_time,TimeDelta rtt){
    if(rtt<min_rtt_){
        min_rtt_=rtt;
    }
    sum_rtt_+=rtt.ToMilliseconds();
    Update_Emplace(ack_time,rtt);
}
}
