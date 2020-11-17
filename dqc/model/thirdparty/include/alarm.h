#pragma once
#include <memory>
#include "proto_time.h"
namespace dqc{
class Alarm{
public:
class Delegate{
public:
    virtual ~Delegate(){}
    virtual void OnAlarm()=0;
};
explicit Alarm(std::unique_ptr<Delegate> d);
Alarm(const Alarm&)=delete;
Alarm& operator=(const Alarm&)=delete;
virtual ~Alarm(){}
  // Sets the alarm to fire at |deadline|.  Must not be called while
  // the alarm is set.  To reschedule an alarm, call Cancel() first,
  // then Set().
void Set(ProtoTime new_deadline);

  // Cancels the alarm.  May be called repeatedly.  Does not
  // guarantee that the underlying scheduling system will remove
  // the alarm's associated task, but guarantees that the
  // delegates OnAlarm method will not be called.
void Cancel();

  // Cancels and sets the alarm if the |deadline| is farther from the current
  // deadline than |granularity|, and otherwise does nothing.  If |deadline| is
  // not initialized, the alarm is cancelled.
void Update(ProtoTime new_deadline, TimeDelta granularity);

  // Returns true if |deadline_| has been set to a non-zero time.
bool IsSet() const;

ProtoTime deadline() const { return deadline_; }
protected:
   virtual void SetImpl()=0;
   virtual void CancelImpl()=0;
   virtual void UpdateImpl()=0;
  // Called by subclasses when the alarm fires.  Invokes the
  // delegates |OnAlarm| if a delegate is set, and if the deadline
  // has been exceeded.  Implementations which do not remove the
  // alarm from the underlying scheduler on Cancel() may need to handle
  // the situation where the task executes before the deadline has been
  // reached, in which case they need to reschedule the task and must not
  // call invoke this method.
void Fire();
private:
    std::unique_ptr<Delegate> delegate_;
    ProtoTime deadline_;
};
class AlarmFactory{
public:
virtual ~AlarmFactory(){}
virtual std::shared_ptr<Alarm> CreateAlarm(std::unique_ptr<Alarm::Delegate> delegate)=0;
};
}
