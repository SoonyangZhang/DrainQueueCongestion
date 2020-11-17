#include <algorithm>
#include "alarm.h"
#include "logging.h"
namespace dqc{
Alarm::Alarm(std::unique_ptr<Delegate> d):
delegate_(std::move(d)),
deadline_(ProtoTime::Zero()){}
void Alarm::Set(ProtoTime new_deadline){
  DCHECK(!IsSet());
  DCHECK(new_deadline.IsInitialized());
  deadline_ = new_deadline;
  SetImpl();
}
void Alarm::Cancel(){
    if(!IsSet()){
        return;
    }
    deadline_=ProtoTime::Zero();
    CancelImpl();
}
void Alarm::Update(ProtoTime new_deadline,
                   TimeDelta granularity){
  if (!new_deadline.IsInitialized()) {
    Cancel();
    return;
  }
  if (std::abs((new_deadline - deadline_).ToMicroseconds()) <
      granularity.ToMicroseconds()) {
    return;
  }
  const bool was_set = IsSet();
  deadline_ = new_deadline;
  if (was_set) {
    UpdateImpl();
  } else {
    SetImpl();
  }
}
bool Alarm::IsSet() const{
    return deadline_.IsInitialized();
}
void Alarm::Fire(){
    if(!IsSet()){
        return ;
    }
    deadline_=ProtoTime::Zero();
    delegate_->OnAlarm();
}
}
