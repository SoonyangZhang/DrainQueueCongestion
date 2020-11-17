#pragma once
#include <deque>
#include <vector>
#include "proto_types.h"
#include "interval.h"
#include "logging.h"
#include "proto_time.h"
namespace dqc{
class PacketQueue{
public:
    PacketQueue(){}
    ~PacketQueue(){
        Clear();
    }
    void Add(PacketNumber p);
    void AddRange(PacketNumber l,PacketNumber h);
    void Clear(){
        packet_deque_.clear();
    }
    bool Contains(PacketNumber p);
    bool Empty() const{
        return packet_deque_.empty();
    }
    bool RemoveUpTo(PacketNumber higher);
    PacketNumber Min() const;
    PacketNumber Max() const;
    void Print();
    typedef std::deque<Interval<PacketNumber>>::iterator iterator;
    typedef std::deque<Interval<PacketNumber>>::const_iterator const_iterator;
    typedef std::deque<Interval<PacketNumber>>::const_reverse_iterator const_reverse_iterator;
    iterator begin(){
        return packet_deque_.begin();
    }
    iterator end(){
        return packet_deque_.end();
    }
    const_reverse_iterator rbegin() const{
        return packet_deque_.rbegin();
    }
    const_reverse_iterator rend() const{
        return packet_deque_.rend();
    }
    size_t size(){
        return packet_deque_.size();
    }
    ByteCount LastIntervalLength() const;
    private:
    std::deque<Interval<PacketNumber>> packet_deque_;
};
typedef std::vector<std::pair<PacketNumber,ProtoTime>> PacketVector;
struct AckFrame{
    AckFrame();
    AckFrame(const AckFrame &o)=default;
    ~AckFrame(){}
    PacketQueue packets;
    PacketVector received_packet_times;
    PacketNumber largest_acked;
    TimeDelta ack_delay_time;
    uint64_t ecn_ce_count{0};
};
inline PacketNumber LargestAcked(const AckFrame& frame) {
  DCHECK((!frame.packets.Empty()) &&frame.packets.Max() > frame.largest_acked);
  return frame.largest_acked;
}
}//namespace dqc;

