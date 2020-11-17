#pragma once
#include "proto_packets.h"
#include "proto_time.h"
#include <deque>
namespace dqc{
class UnackedPacketMap{
public:
      // Returns the sum of bytes from all packets in flight.
    ~UnackedPacketMap(){}
    ByteCount bytes_in_flight() const;
    PacketNumber GetLeastUnacked() const ;
    ByteCount delivered() const {return delivered_;};
    void AddSentPacket(SerializedPacket *packet,PacketNumber old,ProtoTime send_ts,HasRetransmittableData has_retrans);
    TransmissionInfo *GetTransmissionInfo(PacketNumber seq);
    bool IsUnacked(PacketNumber seq);
    ProtoTime GetLastPacketSentTime() const;
    void InvokeLossDetection(AckedPacketVector &packets_acked,LostPacketVector &packets_lost);
    void RemoveFromInflight(PacketNumber seq);
    void RemoveLossFromInflight(PacketNumber seq);
    void RemoveObsolete();
    void AddDelivered(ByteCount acked_bytes){
        delivered_+=acked_bytes;
    }
    typedef std::deque<TransmissionInfo> DequeUnackedPacketMap;
    typedef DequeUnackedPacketMap::const_iterator const_iterator;
    typedef DequeUnackedPacketMap::iterator iterator;

    const_iterator begin() const { return unacked_packets_.begin(); }
    const_iterator end() const { return unacked_packets_.end(); }
    iterator begin() { return unacked_packets_.begin(); }
    iterator end() { return unacked_packets_.end(); }
private:
    DequeUnackedPacketMap unacked_packets_;
    PacketNumber least_unacked_;
    ByteCount bytes_in_flight_{0};
    PacketNumber  largest_newly_acked_;
    ByteCount delivered_{0};
};
}//namespace dqc;
