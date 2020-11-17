#pragma once
#include <deque>
#include "proto_time.h"
#include "packet_number.h"
namespace dqc{
struct RttSample{
RttSample(ProtoTime now,TimeDelta delta)
:event(now),rtt(delta){}
ProtoTime event;

TimeDelta rtt;
};
class RttMonitor{
public:
    RttMonitor(TimeDelta length):monitor_length_(length)
    {
    }
    ~RttMonitor(){}
    void Update(ProtoTime ack_time,TimeDelta rtt);
    void UpdateDuration(TimeDelta length){
		monitor_length_=length;
	}
    TimeDelta GetMinRtt ()const{
        return min_rtt_;
    }
    size_t Size() const{
        return samples_.size();
    }
    void Clear(){
        min_rtt_=TimeDelta::Infinite();
        sum_rtt_=0;
        samples_.clear();
    }
    TimeDelta Average() const;
private:
    template<class ...Args>
    void Update_Emplace(Args&& ...args){
        samples_.emplace_back(std::forward<Args>(args)...);
        ProtoTime newest=samples_.back().event;
        while(!samples_.empty()){
            auto it=samples_.begin();
            ProtoTime event_ts=it->event;
            if(newest-event_ts>monitor_length_){
                sum_rtt_-=it->rtt.ToMilliseconds();
                samples_.erase(it);
            }else{
                break;
            }
        }
    }
    std::deque<RttSample> samples_;
    TimeDelta monitor_length_;
    int64_t sum_rtt_{0};
    TimeDelta min_rtt_{TimeDelta::Infinite()};
};
}
