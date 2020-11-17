#include "process_alarm_factory.h"
#include "logging.h"
#include <stdio.h>
namespace dqc{
MainEngine::~MainEngine(){
    SetStop();
    while(!cbs_.empty()){
        auto it=cbs_.begin();
		it->second->RemoveByEngine();
        cbs_.erase(it);
    }
}
void MainEngine::HeartBeat(ProtoTime time){
    TimeDelta wall_time=time-ProtoTime::Zero();
    int64_t now=wall_time.ToMilliseconds();
    std::vector<std::pair<AlarmCb*,int64_t>> registers;
    while(!cbs_.empty()){
        auto it=cbs_.begin();
        AlarmCb *cb=it->second;
        int64_t timeout=it->first;
        if(timeout>now){
            break;
        }
        int64_t time=cb->OnAlarm();
        if(time>0){
            registers.push_back(std::make_pair(cb,time));
        }
        cbs_.erase(it);
    }
    if(!registers.empty()){
        for(auto it=registers.begin();it!=registers.end();it++){
            RegisterAlarm(it->second,it->first);
        }
    }
}
void MainEngine::ExecuteCallback(ProtoTime time){
	HeartBeat(time);
}
ProtoTime MainEngine::PeekNextEventTime(){
	ProtoTime nextTime=ProtoTime::Infinite();
	if(!cbs_.empty()){
        auto it=cbs_.begin();
        int64_t time=it->first;
        nextTime=ProtoTime::Zero()+TimeDelta::FromMilliseconds(time);
	}
	return nextTime;
}
void MainEngine::RegisterAlarm(int64_t &time_out,AlarmCb* alarm){
    if(stop_){
        return ;
    }
    MainEngine::iterator ret=cbs_.insert(std::make_pair(time_out,alarm));
    alarm->OnRegistered(ret,this);
}
MainEngine::iterator MainEngine::RegisterAlarm(MainEngine::iterator token,int64_t &time_out){
    uint64_t origin=token->first;
    AlarmCb *cb=token->second;
    sz_type entries=cbs_.count(origin);
    iterator beg=cbs_.find(origin);
    bool found=false;
    for(sz_type i=0;i<entries;i++,beg++){
        if(beg->second==cb){
            found=true;
            break;
        }
    }
    DCHECK(found);
    cbs_.erase(beg);
    auto ret=cbs_.insert(std::make_pair(time_out,cb));
    return ret;
}
void MainEngine::UnRegister(MainEngine::iterator token){
    uint64_t origin=token->first;
    AlarmCb *cb=token->second;
    sz_type entries=cbs_.count(origin);
    iterator beg=cbs_.find(origin);
    bool found=false;
    for(sz_type i=0;i<entries;i++,beg++){
        if(beg->second==cb){
            found=true;
            break;
        }
    }
    DCHECK(found);
    cbs_.erase(beg);
}
class ProcessAlarm:public AlarmCb::AlarmInterface,public Alarm{
public:
    ProcessAlarm(MainEngine* engine,std::unique_ptr<Alarm::Delegate> delegate):
    Alarm(std::move(delegate)),
    cb_(this),
    engine_(engine){}
    ~ProcessAlarm(){
        CancelImpl();
    }
    int64_t OnAlarm() override{
        Fire();
        return 0;
    }
    void SetImpl() override{
        TimeDelta delta=deadline()-ProtoTime::Zero();
        int64_t timeout=delta.ToMilliseconds();
        engine_->RegisterAlarm(timeout,&cb_);
    }
    void CancelImpl() override{
        cb_.Cancel();
    }
    void UpdateImpl() override{
        TimeDelta delta=deadline()-ProtoTime::Zero();
        int64_t timeout=delta.ToMilliseconds();
        if(cb_.registered()){
            cb_.Reregistered(timeout);
        }else{
            engine_->RegisterAlarm(timeout,&cb_);
        }
    }
private:
    AlarmCb cb_;
    MainEngine* engine_;
};
void AlarmCb::OnRegistered(MainEngine::iterator token,
                           MainEngine *engine){

	//token_=token_; bug find, fuck, this cost me nearly one hour.
    token_=token;
    engine_=engine;
    registered_=true;
}
void AlarmCb::Reregistered(int64_t &time_out){
    if(engine_->IsStop()){
        return;
    }
    token_=engine_->RegisterAlarm(token_,time_out);
    registered_=true;
}
void AlarmCb::Cancel(){
    if(registered_){
        engine_->UnRegister(token_);
    }
    registered_=false;
}
int64_t AlarmCb::OnAlarm(){
    registered_=false;
    return alarm_->OnAlarm();
}
std::shared_ptr<Alarm> ProcessAlarmFactory::CreateAlarm(std::unique_ptr<Alarm::Delegate> delegate){
    Alarm *alarm=new ProcessAlarm(engine_,std::move(delegate));
    return std::shared_ptr<Alarm>(alarm);
}
}
