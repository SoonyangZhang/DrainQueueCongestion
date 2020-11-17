#pragma once
#include "proto_comm.h"
#include "proto_time.h"
#include <vector>
namespace dqc{
struct PacketStream{
PacketStream():PacketStream(-1,0,0,false){}
PacketStream(uint32_t id,StreamOffset offset,PacketLength len,bool fin)
:PacketStream(id,offset,nullptr,len,fin){}
PacketStream(uint32_t id1,StreamOffset offset1,char*data1,PacketLength len1,bool fin1)
:stream_id(id1),offset(offset1),len(len1),fin(fin1),data_buffer(data1){}
uint32_t stream_id;
StreamOffset offset;
PacketLength len;
bool fin;
const char *data_buffer{nullptr};
};
class ProtoFrame{
public:
explicit ProtoFrame(PacketStream frame)
:type_(PROTO_FRAME_STREAM)
,stream_frame(frame){
}
explicit ProtoFrame(ProtoFrameType type)
:type_(type){}
ProtoFrameType type() const{
	return type_;
}
const PacketStream& StreamInfo() const{
	return stream_frame;
}
private:
ProtoFrameType type_;
PacketStream stream_frame;
};
typedef std::vector<ProtoFrame> ProtoFrames;
struct SerializedPacket{
PacketNumber  number;
char *buf;
PacketLength len;
bool has_ack;
bool has_stop_waitting;
ProtoFrames retransble_frames;
};
bool HasStreamInRetransbleFrames(ProtoFrames &frame);
struct TransmissionInfo{
TransmissionInfo()
:sent_time(ProtoTime::Zero()),bytes_sent(0),inflight(false),state(SPS_NERVER_SENT){}
TransmissionInfo(ProtoTime send_time,PacketLength bytes_sent)
:sent_time(send_time),bytes_sent(bytes_sent),inflight(false),state(SPS_OUT){}
ProtoTime sent_time;
PacketLength bytes_sent;
bool inflight;
SentPacketState state;
ProtoFrames retransble_frames;
};
struct PendingRetransmission{
PacketNumber  number;
ProtoFrames retransble_frames;
};
class ProtoReceivedPacket{
public:
    ProtoReceivedPacket(char *buf,int len,ProtoTime receipe)
    :ProtoReceivedPacket(buf,len,receipe,false){}
    ProtoReceivedPacket(char *buf,int len,ProtoTime receipe,bool own_buffer)
    :packet_(buf)
    ,length_(len)
    ,receipe_time_(receipe)
    ,own_buffer_(own_buffer){}
    ~ProtoReceivedPacket(){
        if(own_buffer_&&packet_){
            delete [] static_cast<char*>(packet_);
        }
    }
    ProtoReceivedPacket(const ProtoReceivedPacket &o)=delete;
    ProtoReceivedPacket &operator=(const ProtoReceivedPacket &o)=delete;
    char *packet() const{ return packet_;}
    int length() const {return length_;}
    ProtoTime receipe_time() const{ return receipe_time_;}
private:
    char *packet_{nullptr};
    int length_{0};
    ProtoTime receipe_time_;
    bool own_buffer_{false};
};
}//namespace dqc;
