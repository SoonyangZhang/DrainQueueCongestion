#include "unacked_packet_map.h"
#include "logging.h"
namespace dqc{
ByteCount  UnackedPacketMap::bytes_in_flight() const  { return bytes_in_flight_; }
PacketNumber  UnackedPacketMap::GetLeastUnacked() const { return least_unacked_;}
void UnackedPacketMap::AddSentPacket(SerializedPacket *packet,PacketNumber old,ProtoTime send_ts,HasRetransmittableData has_retrans)
{
    if(!least_unacked_.IsInitialized()){
        least_unacked_=packet->number;
    }
    // packet sent in none continue mode
    while(least_unacked_+unacked_packets_.size()<packet->number){
        DLOG(INFO)<<"seen unsent ";
        unacked_packets_.push_back(TransmissionInfo());
    }
    PacketLength bytes_sent=packet->len;
    if(has_retrans!=HAS_RETRANSMITTABLE_DATA){
    	bytes_sent=0;
    }
    TransmissionInfo info(send_ts,bytes_sent);
    bytes_in_flight_+=bytes_sent;
    info.inflight=true;
    unacked_packets_.push_back(info);
    unacked_packets_.back().retransble_frames.swap(packet->retransble_frames);
}
TransmissionInfo *UnackedPacketMap::GetTransmissionInfo(PacketNumber seq){
    TransmissionInfo *info=nullptr;
    if(!least_unacked_.IsInitialized()||seq<least_unacked_||(least_unacked_+unacked_packets_.size())<=seq){
        DLOG(INFO)<<"null info unack "<<least_unacked_<<" "<<seq;
        return info;
    }
    info=&unacked_packets_[seq-least_unacked_];
    return info;
}
bool UnackedPacketMap::IsUnacked(PacketNumber seq){
    if(!least_unacked_.IsInitialized()){
        return false;
    }
    if(seq<least_unacked_||(least_unacked_+unacked_packets_.size())<=seq){
        return false;
    }
    TransmissionInfo *info=GetTransmissionInfo(seq);
    if(info&&info->state==SPS_OUT){
        return true;
    }
    return false;
}
ProtoTime UnackedPacketMap::GetLastPacketSentTime() const{
    auto it=unacked_packets_.rbegin();
    ProtoTime sent_time(ProtoTime::Zero());
    while(it!=unacked_packets_.rend()){
        if(it->inflight){
            sent_time=it->sent_time;
            break;
        }
        it++;
    }
    return sent_time;
}
void UnackedPacketMap::InvokeLossDetection(AckedPacketVector &packets_acked,LostPacketVector &packets_lost){
    if(packets_acked.empty()){
        return;
    }
    if(!packets_acked.empty()){
        largest_newly_acked_=packets_acked.back().packet_number;
    }
    auto acked_it=packets_acked.begin();
    PacketNumber first_seq=acked_it->packet_number;
    DCHECK(first_seq>=least_unacked_);
    TransmissionInfo *info;
    PacketNumber i=least_unacked_;
    for(;i<first_seq;i++){
        info=GetTransmissionInfo(i);
        if(info&&info->inflight&&(info->state==SPS_OUT)){
            packets_lost.emplace_back(i,info->bytes_sent);
            info->state=SPS_LOST;
        }
    }
    for(;acked_it!=packets_acked.end();acked_it++){
        auto next=acked_it+1;
        if(next!=packets_acked.end()){
            PacketNumber left=acked_it->packet_number;
            PacketNumber right=next->packet_number;
            //DLOG(WARNING)<<left<<" "<<right;
            DCHECK(left<right);
            for(i=left+1;i<right;i++){
                info=GetTransmissionInfo(i);
                if(info&&info->inflight){
                    packets_lost.emplace_back(i,info->bytes_sent);
                }
            }
        }
    }
}
void UnackedPacketMap::RemoveFromInflight(PacketNumber seq){
    TransmissionInfo *info=GetTransmissionInfo(seq);
    if(info&&info->inflight){
        ByteCount bytes_sent=info->bytes_sent;
        bytes_sent=std::min(bytes_sent,bytes_in_flight_);
        bytes_in_flight_-=bytes_sent;
        info->inflight=false;
    }
}
void UnackedPacketMap::RemoveLossFromInflight(PacketNumber seq){
    RemoveFromInflight(seq);
}
void UnackedPacketMap::RemoveObsolete(){
    while(!unacked_packets_.empty()){
		const TransmissionInfo& info=unacked_packets_.front();
        if(info.inflight&&(info.state==SPS_OUT)){
            break;
        }
        unacked_packets_.pop_front();
        least_unacked_++;
    }
}
}//namespace dqc;
