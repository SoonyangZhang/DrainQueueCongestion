#include "ack_frame.h"
namespace dqc{
void PacketQueue::Add(PacketNumber p){
    if(packet_deque_.empty()){
        packet_deque_.push_front(Interval<PacketNumber>(p,p+1));
        return;
    }
    Interval<PacketNumber> back=packet_deque_.back();
    if(back.Max()==p){
        packet_deque_.back().SetMax(p+1);
        return;
    }
    if(back.Max()<p){
    packet_deque_.push_back(Interval<PacketNumber>(p,p+1));
        return;
    }
    Interval<PacketNumber> front=packet_deque_.front();
    if(front.Min()>p+1){
        packet_deque_.push_front(Interval<PacketNumber>(p,p+1));
        return;
    }
    if(front.Min()==p+1){
        packet_deque_.front().SetMin(p);
        return;
    }
    int i=packet_deque_.size()-1;
    while(i>=0){
        Interval<PacketNumber> temp=packet_deque_[i];
        if(temp.Contains(p)){
            return;
        }
        if(temp.Max()==p){
            packet_deque_[i].SetMax(p+1);
            return;
        }
        Interval<PacketNumber> before=packet_deque_[i-1];
        if(temp.Min()==p+1){
            packet_deque_[i].SetMin(p);
            if(i>0&&before.Max()==p){
                packet_deque_[i-1].SetMax(temp.Max());
                packet_deque_.erase(packet_deque_.begin()+i);
            }
            return;
        }
        if(temp.Max()<p){
            packet_deque_.insert(packet_deque_.begin()+i+1,Interval<PacketNumber>(p,p+1));
        }
        i--;

    }
    return ;
}
void PacketQueue::AddRange(PacketNumber l,PacketNumber h){
//seems quite useless;
    if(l>h){
        return;
    }
    PacketNumber i=l;
    for(i=l;i<=h;i++){
        Add(i);
    }

}
bool PacketQueue::Contains(PacketNumber p){
    if(packet_deque_.empty()){
        return false;
    }
    Interval<PacketNumber> front=packet_deque_.front();
    Interval<PacketNumber> back=packet_deque_.back();
    if(front.Min()>p||back.Max()<=p){
        return false;
    }
    int len=packet_deque_.size();
    int i=0;
    for(i=0;i<len;i++){
        Interval<PacketNumber> temp=packet_deque_[i];
        if(temp.Contains(p)){
            return true;
        }
    }
    return false;
}
bool PacketQueue::RemoveUpTo(PacketNumber higher){
    if(packet_deque_.empty()){
        return false;
    }
    const PacketNumber old_min = Min();
    while(!packet_deque_.empty()){
        Interval<PacketNumber> front=packet_deque_.front();
        if(front.max()<higher){
            packet_deque_.pop_front();
        }else if(front.min()<higher&&front.max()>=higher){
            packet_deque_.front().SetMin(higher);
            if(higher==front.max()){
                packet_deque_.pop_front();
            }
            break;
        }else{
            break;
        }
    }
    return Empty()||old_min!=Min();
}
PacketNumber PacketQueue::Min() const{
    return packet_deque_.front().Min();
}
PacketNumber PacketQueue::Max() const{
    return packet_deque_.back().Max();
}
void PacketQueue::Print(){
    if(packet_deque_.empty()){
        return;
    }
    int len=packet_deque_.size();
    int i=0;
    for(i=0;i<len;i++){
        Interval<PacketNumber> temp=packet_deque_[i];
        DLOG(INFO)<<temp.Min()<<" "<<temp.Max();

    }
}
ByteCount PacketQueue::LastIntervalLength() const{
    DCHECK(!Empty());
    return packet_deque_.back().Length();
}
AckFrame::AckFrame()
:largest_acked(0),ack_delay_time(TimeDelta::Infinite()){}
}//namespace dqc;
