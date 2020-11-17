#include "send_packet_manager.h"
#include "proto_constants.h"
#include "logging.h"
#include <ctime>
#include <algorithm>
namespace dqc{
namespace{
static const int64_t kDefaultRetransmissionTimeMs = 500;
static const int64_t kMaxRetransmissionTimeMs = 60000;
// Maximum number of exponential backoffs used for RTO timeouts.
static const size_t kMaxRetransmissions = 10;
// Maximum number of packets retransmitted upon an RTO.
static const size_t kMaxRetransmissionsOnTimeout = 2;
// The path degrading delay is the sum of this number of consecutive RTO delays.
const size_t kNumRetransmissionDelaysForPathDegradingDelay = 2;
const int32_t kMaxFastRetransNum=2;
static int kRandSeedOffset=9301; 
static int kRandomCount=0;
}
//only retransmitable frame can be marked as inflight;
//hence, only stream has such quality.
SendPacketManager::SendPacketManager(ProtoClock *clock,QuicConnectionStats* stats,StreamAckedObserver *acked_observer)
:clock_(clock)
,stats_(stats)
,acked_observer_(acked_observer)
,min_rto_timeout_(TimeDelta::FromMilliseconds(kMinRetransmissionTimeMs))
,one_way_delay_(PacketNumber(0),TimeDelta::Zero()){
    DCHECK(clock_);
	int seed=std::time(nullptr);
    rand_.seed(seed+kRandSeedOffset*kRandomCount);
	kRandomCount+=2;
}
SendPacketManager::~SendPacketManager(){
}
void SendPacketManager::SetSendAlgorithm(CongestionControlType congestion_control_type){
    send_algorithm_.reset(SendAlgorithmInterface::Create(clock_,
                                                   &rtt_stats_,
                                                   &unacked_packets_,
                                                   congestion_control_type,&rand_,
												   stats_,
                                                   kMinInitialCongestionWindow));

    pacing_sender_.set_sender(send_algorithm_.get());
}
void SendPacketManager::SetSendAlgorithm(SendAlgorithmInterface* send_algorithm){
    send_algorithm_.reset(send_algorithm);
    pacing_sender_.set_sender(send_algorithm);
}
bool SendPacketManager::OnSentPacket(SerializedPacket *packet,PacketNumber old,
                      HasRetransmittableData has_retrans,ProtoTime send_ts){
    //bool in_flight=(has_retrans==HAS_RETRANSMITTABLE_DATA);
    PacketNumber packet_number = packet->number;
    if(using_pacing_){
        pacing_sender_.OnPacketSent(send_ts,unacked_packets_.bytes_in_flight(),
                                    packet_number,packet->len,has_retrans);
    }else{
        send_algorithm_->OnPacketSent(send_ts,unacked_packets_.bytes_in_flight(),
                                    packet_number,packet->len,has_retrans);
    }
    unacked_packets_.AddSentPacket(packet,old,send_ts,has_retrans);
    return true;
}
const TimeDelta SendPacketManager::TimeUntilSend(ProtoTime now) const{
    if(using_pacing_){
        return pacing_sender_.TimeUntilSend(now,
                                            unacked_packets_.bytes_in_flight());
    }
    return send_algorithm_->CanSend(unacked_packets_.bytes_in_flight())?TimeDelta::Zero():TimeDelta::Infinite();
}
bool SendPacketManager::HasPendingForRetrans(){
    return !pendings_.empty();
}
PendingRetransmission SendPacketManager::NextPendingRetrans(){
    PendingRetransmission pending;
    PendingRetransmissionMap::iterator found=pendings_.begin();
    pending.number=found->first;
    found->second.retransble_frames.swap(pending.retransble_frames);
    pendings_.erase(found);
    return pending;

}
void SendPacketManager::Retransmitted(PacketNumber number){
    PendingRetransmissionMap::iterator found=pendings_.find(number);
    if(found!=pendings_.end()){
        pendings_.erase(found);
    }else{
        DLOG(WARNING)<<number<<" not exist";
    }
}
void SendPacketManager::OnRetransmissionTimeOut(){
    ++consecutive_rto_count_;
    //send_algorithm_->OnRetransmissionTimeout(true);
}
void SendPacketManager::FastRetransmit(){
	if(has_in_flight()){
		int delivered=DeliverPacketsToPendingQueue(kMaxFastRetransNum);
		unacked_packets_.RemoveObsolete();
		if(delivered>0){
			fast_retrans_flag_=true;
		}
	}
}
int SendPacketManager::DeliverPacketsToPendingQueue(int n){
	int delivered=0;
    PacketNumber seq=unacked_packets_.GetLeastUnacked();
    if(!seq.IsInitialized()){
        return delivered;
    }
    int count=0;
    for(auto it=pendings_.begin();it!=pendings_.end();it++){
    	if(HasStreamInRetransbleFrames(it->second.retransble_frames)){
    		count++;
    		if(count>=n){
    			delivered=n;
    			return delivered;
    		}
    	}
    }
    for(auto it=unacked_packets_.begin();it!=unacked_packets_.end();it++){
        if(it->inflight&&(it->state==SPS_OUT)&&HasStreamInRetransbleFrames((*it).retransble_frames)){
            it->state=SPS_RETRANSED;
            PostToPending(seq,*it);
            count++;
			if(count>=n){break;}
        }
        seq++;
    }
	delivered=count;
	CHECK(delivered);
	return delivered;
}
void SendPacketManager::UpdateEcnBytes(uint64_t ecn_ce_count){
    send_algorithm_->OnUpdateEcnBytes(ecn_ce_count);
}
void SendPacketManager::OnAckStart(PacketNumber largest_acked,TimeDelta ack_delay_time,ProtoTime ack_receive_time){
    DCHECK(packets_acked_.empty());
	if(!largest_acked_.IsInitialized()){
		largest_acked_=largest_acked;
	}else{
		if(largest_acked==largest_acked_){
			//FastRetransmit();
		}
		if(largest_acked>largest_acked_){
			largest_acked_=largest_acked;
		}
	}
    rtt_updated_=MaybeUpdateRTT(largest_acked,ack_delay_time,ack_receive_time);
    ack_packet_itor_=last_ack_frame_.packets.rbegin();
}
void SendPacketManager::OnAckRange(PacketNumber start,PacketNumber end){
    PacketNumber least_unakced=unacked_packets_.GetLeastUnacked();
    if(end<=least_unakced){
        return ;
    }
    start=std::max(start,least_unakced);
    PacketNumber acked=end-1;
    PacketNumber newly_acked_start=start;
    if(ack_packet_itor_!=last_ack_frame_.packets.rend()){
        newly_acked_start=std::max(start,ack_packet_itor_->Max());
    }
    for(;acked>=newly_acked_start;acked--){
        packets_acked_.push_back(AckedPacket(acked,0,ProtoTime::Zero()));
    }
}
void SendPacketManager::OnAckTimestamp(PacketNumber packet_number,ProtoTime timestamp){
  last_ack_frame_.received_packet_times.push_back({packet_number, timestamp});
  for (AckedPacket& packet : packets_acked_) {
    if (packet.packet_number == packet_number) {
      packet.receive_ts = timestamp;
      return;
    }
  }
}
AckResult SendPacketManager::OnAckEnd(ProtoTime ack_receive_time){
    
    PacketNumber sent_seq=PacketNumber(0);
    ProtoTime receive_time=ProtoTime::Zero();
    if(!last_ack_frame_.received_packet_times.empty()){
        auto it=last_ack_frame_.received_packet_times.begin();
        sent_seq=it->first;
        receive_time=it->second;
    }
	last_ack_frame_.received_packet_times.clear();
    ByteCount prior_bytes_in_flight = unacked_packets_.bytes_in_flight();
    std::reverse(packets_acked_.begin(),packets_acked_.end());
    ByteCount acked_bytes=0;
    for(auto it=packets_acked_.begin();it!=packets_acked_.end();it++){
        PacketNumber seq=it->packet_number;
        TransmissionInfo *info=unacked_packets_.GetTransmissionInfo(seq);
        if(!info){
            DLOG(WARNING)<<"acked unsent packet "<<seq;
        }else{
            acked_bytes+=info->bytes_sent;
            if(seq==sent_seq&&(receive_time>info->sent_time)){
                one_way_delay_.first=seq;
                one_way_delay_.second=receive_time-info->sent_time;
                sent_time_=info->sent_time;
                recv_time_=receive_time;
            }
            if(info->inflight){
                if(SPS_OUT==info->state){info->state=SPS_ACKED;}
                if(acked_observer_){
                    for(auto frame_it=info->retransble_frames.begin();
                    frame_it!=info->retransble_frames.end();frame_it++){
                    	if(frame_it->type()==PROTO_FRAME_STREAM){
                    		const PacketStream &stream=frame_it->StreamInfo();
                            acked_observer_->OnAckStream(stream.stream_id,
                            		stream.offset,stream.len);
                    	}
                    }
                }
			   it->bytes_acked=info->bytes_sent;
               unacked_packets_.RemoveFromInflight(seq);
            }
            last_ack_frame_.packets.Add(seq);
        }
    }
    const bool acked_new_packet = !packets_acked_.empty();
    unacked_packets_.AddDelivered(acked_bytes);
    PostProcessNewlyAckedPackets(ack_receive_time,rtt_updated_,prior_bytes_in_flight);
    sent_time_=ProtoTime::Zero();
    recv_time_=ProtoTime::Zero();
    return acked_new_packet? PACKETS_NEWLY_ACKED : NO_PACKETS_NEWLY_ACKED;
}
class PacketGenerator{
public:
    PacketGenerator(){}
    SerializedPacket CreateStream(){
        PacketStream stream(id_,offset_,stream_len_,false);
        offset_+=stream_len_;
        ProtoFrame frame(stream);
        SerializedPacket packet;
        packet.number=AllocSeq();
        packet.len=stream_len_;
        packet.retransble_frames.push_back(frame);
        return packet;
    }
    SerializedPacket CreateAck(){
        SerializedPacket packet;
        packet.number=AllocSeq();
        packet.len=ack_len_;
        return packet;
    }
private:
    PacketNumber AllocSeq(){
        return seq_++;
    }
    uint32_t id_{0};
    PacketNumber seq_{1};
    uint32_t ack_len_{20};
    uint32_t stream_len_{1000};
    uint64_t offset_{0};
};
void SendPacketManager::MaybeInvokeCongestionEvent(bool rtt_updated,
                                                   ByteCount prior_in_flight,
                                                   ProtoTime event_time){
  if (!rtt_updated && packets_acked_.empty() && packets_lost_.empty()) {
    return;
  }
  if (using_pacing_) {
    if(recv_time_>ProtoTime::Zero()){
        pacing_sender_.OnOneWayDelaySample(event_time,one_way_delay_.first,sent_time_,recv_time_);
    }
    pacing_sender_.OnCongestionEvent(rtt_updated, prior_in_flight, event_time,
                                     packets_acked_, packets_lost_);
  } else {
    if(recv_time_>ProtoTime::Zero()){
        send_algorithm_->OnOneWayDelaySample(event_time,one_way_delay_.first,sent_time_,recv_time_);
    }
    send_algorithm_->OnCongestionEvent(rtt_updated, prior_in_flight, event_time,
                                       packets_acked_, packets_lost_);
  }
}
void SendPacketManager::Test(){
    PacketGenerator generator;
    int i=0;
    for(i=1;i<=10;i+=2){
    SerializedPacket stream=generator.CreateStream();
    SerializedPacket ack=generator.CreateAck();
    OnSentPacket(&stream,PacketNumber(0),HAS_RETRANSMITTABLE_DATA,ProtoTime::Zero());
    OnSentPacket(&ack,PacketNumber(0),HAS_RETRANSMITTABLE_DATA,ProtoTime::Zero());
    }
//lost 2 4, only lost ack frame
//reference from test_proto_framer
    OnAckStart(PacketNumber(10),TimeDelta::Zero(),ProtoTime::Zero());
    OnAckRange(PacketNumber(8),PacketNumber(11));
    OnAckRange(PacketNumber(5),PacketNumber(7));
    OnAckRange(PacketNumber(3),PacketNumber(4));
    OnAckRange(PacketNumber(1),PacketNumber(2));
    OnAckEnd(ProtoTime::Zero());
    /*for(i=1;i<=5;i++){
    SerializedPacket stream=generator.CreateStream();
    OnSentPacket(&stream,0,CON_RE_YES,ProtoTime::Zero());
    }*/
    if(HasPendingForRetrans()){
        DLOG(INFO)<<"has retrans "<<GetLeastUnacked();
    }
}
void SendPacketManager::Test2(){
 if(HasPendingForRetrans()){
        PendingRetransmission pending=
        NextPendingRetrans();
        int len=pending.retransble_frames.size();
        StreamOffset off=0;
        if(len>0){
        	ProtoFrameType type=pending.retransble_frames.front().type();
        	if(type==PROTO_FRAME_STREAM){
            	const PacketStream &stream=pending.retransble_frames.front().StreamInfo();
                off=stream.offset;
        	}
        }
        DLOG(INFO)<<len<<" "<<off;
        Retransmitted(pending.number);
}
    PacketNumber least_unacked=unacked_packets_.GetLeastUnacked();
    DLOG(INFO)<<"least unacked "<<least_unacked;
    TransmissionInfo *info=unacked_packets_.GetTransmissionInfo(least_unacked);
    if(!info){
        DLOG(INFO)<<"not sent";
    }
}
bool SendPacketManager::MaybeUpdateRTT(PacketNumber largest_acked,
                      TimeDelta ack_delay_time,
                      ProtoTime ack_receive_time){
    if(!unacked_packets_.IsUnacked(largest_acked)){
        return false;
    }
    const TransmissionInfo *info=unacked_packets_.GetTransmissionInfo(largest_acked);
    DCHECK(info);
    if(info->sent_time==ProtoTime::Zero()){
        return false;
    }
    TimeDelta send_delta=ack_receive_time-info->sent_time;
    rtt_stats_.UpdateRtt(send_delta,ack_delay_time,ack_receive_time);
    return true;
}
void SendPacketManager::PostProcessNewlyAckedPackets(ProtoTime ack_receive_time,
                                      bool rtt_updated,
                                      ByteCount prior_bytes_in_flight){
    InvokeLossDetection(ack_receive_time);
    unacked_packets_.RemoveObsolete();
    MaybeInvokeCongestionEvent(rtt_updated, prior_bytes_in_flight,
                             ack_receive_time);
    ClearAckedAndLossVector();
    if(rtt_updated){
        consecutive_rto_count_=0;
    }
}
void SendPacketManager::InvokeLossDetection(ProtoTime time){
    unacked_packets_.InvokeLossDetection(packets_acked_,packets_lost_);
    for(auto it=packets_lost_.begin();it!=packets_lost_.end();it++){
        MarkForRetrans(it->packet_number);
        if(trace_lost_){
            uint32_t rtt=rtt_stats_.smoothed_rtt().ToMilliseconds();
            trace_lost_(it->packet_number,rtt);
        }
    }
    // in repeat acked case;
    PacketNumber least_unacked=unacked_packets_.GetLeastUnacked();
    last_ack_frame_.packets.RemoveUpTo(least_unacked);
    last_ack_frame_.received_packet_times.clear();
}
void SendPacketManager::MarkForRetrans(PacketNumber seq){
    DLOG(WARNING)<<"l "<<seq;
    TransmissionInfo *info=unacked_packets_.GetTransmissionInfo(seq);
    if(info&&info->inflight){
        TransmissionInfo copy;
        copy.bytes_sent=info->bytes_sent;
        copy.inflight=info->inflight;
        copy.sent_time=info->sent_time;
        copy.retransble_frames.swap(info->retransble_frames);
        unacked_packets_.RemoveLossFromInflight(seq);
        pendings_[seq]=copy;
    }
}
void SendPacketManager::PostToPending(PacketNumber seq,TransmissionInfo &info){
	unacked_packets_.RemoveLossFromInflight(seq);//for fast retrans
    pendings_[seq]=info;
}
void SendPacketManager::ClearAckedAndLossVector(){
    LostPacketVector temp1;
    packets_lost_.swap(temp1);
    AckedPacketVector temp2;
    packets_acked_.swap(temp2);
}
const TimeDelta SendPacketManager::GetRetransmissionDelay(size_t consecutive_rto_count) const{
  TimeDelta retransmission_delay = TimeDelta::Zero();
  if (rtt_stats_.smoothed_rtt().IsZero()) {
    // We are in the initial state, use default timeout values.
    retransmission_delay =
        TimeDelta::FromMilliseconds(kDefaultRetransmissionTimeMs);
  } else {
    retransmission_delay =
        rtt_stats_.smoothed_rtt() + 4 * rtt_stats_.mean_deviation();
    if (retransmission_delay < min_rto_timeout_) {
      retransmission_delay = min_rto_timeout_;
    }
  }

  // Calculate exponential back off.
  retransmission_delay =
      retransmission_delay *
      (1 << std::min<size_t>(consecutive_rto_count, kMaxRetransmissions));

  if (retransmission_delay.ToMilliseconds() > kMaxRetransmissionTimeMs) {
    return TimeDelta::FromMilliseconds(kMaxRetransmissionTimeMs);
  }
  return retransmission_delay;
}
void SendPacketManager::SetCongestionId(uint32_t cid){
    if(send_algorithm_){
        send_algorithm_->SetCongestionId(cid);
    }
}
void SendPacketManager::SetNumEmulatedConnections(int num_connections){
    if(send_algorithm_){
        send_algorithm_->SetNumEmulatedConnections(num_connections);
    }
}
}//namespace dqc;
