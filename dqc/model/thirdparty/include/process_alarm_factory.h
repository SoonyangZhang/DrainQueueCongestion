#pragma once
#include "alarm.h"
#include "proto_time.h"
#include <map>
namespace dqc{
class AlarmCb;
class MainEngine{
public:
    typedef std::multimap<int64_t,AlarmCb*>::iterator iterator;
    typedef std::multimap<int64_t,AlarmCb*>::size_type sz_type;
    MainEngine(){}
    ~MainEngine();
    void HeartBeat(ProtoTime time);
    void ExecuteCallback(ProtoTime time);
    ProtoTime PeekNextEventTime();
    void RegisterAlarm(int64_t &time_out,AlarmCb* alarm);
    iterator RegisterAlarm(iterator token,int64_t &time_out);
    void UnRegister(iterator token);
    void SetStop(){
        stop_=true;
    }
    bool IsStop() const{
        return stop_;
    }
private:
    std::multimap<int64_t,AlarmCb*> cbs_;
    bool stop_{false};
};
class AlarmCb{
public:
class AlarmInterface{
public:
    virtual ~AlarmInterface(){}
    virtual int64_t OnAlarm()=0;
};
    AlarmCb(AlarmInterface *alarm):alarm_(alarm){}
    ~AlarmCb(){}
    void RemoveByEngine(){
        registered_=false;
    }
    bool registered() const {return registered_;}
    void OnRegistered(MainEngine::iterator token,MainEngine *engine);
    void Reregistered(int64_t &time_out);
    void Cancel();
    int64_t OnAlarm();
private:
    bool registered_{false};
    MainEngine::iterator token_;
    MainEngine *engine_{nullptr};
    AlarmInterface *alarm_{nullptr};
};
class ProcessAlarmFactory: public AlarmFactory{
public:
    ProcessAlarmFactory(MainEngine *engine):
engine_(engine){}
    ~ProcessAlarmFactory(){}
    virtual std::shared_ptr<Alarm> CreateAlarm(std::unique_ptr<Alarm::Delegate> delegate) override;
private:
    MainEngine *engine_{nullptr};
};
}
