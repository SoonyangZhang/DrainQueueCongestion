#include <memory.h>
#include "proto_con.h"
#include "proto_utils.h"
#include "byte_codec.h"
#include "flag_impl.h"
#include "ns3/log.h"
NS_LOG_COMPONENT_DEFINE("proto_connection");
using namespace ns3;
namespace dqc{
namespace {
    const int kMinReleaseTimeIntoFutureMs = 1;
}
class SendAlarmDelegate:public Alarm::Delegate{
public:
    SendAlarmDelegate(ProtoCon *connection)
    :connection_(connection){}
    ~SendAlarmDelegate(){}
    void OnAlarm() override{
        connection_->OnCanWrite();
    }
private:
    ProtoCon *connection_;
};
class FastRetransDelegate:public Alarm::Delegate{
public:
	FastRetransDelegate(ProtoCon *connection)
    :connection_(connection){}
	~FastRetransDelegate(){}
	void OnAlarm() override{
		connection_->OnFastRetransmit();
	}
private:
	ProtoCon *connection_;
};
ProtoCon::ProtoCon(ProtoClock *clock,AlarmFactory *alarm_factory,CongestionControlType cc)
:clock_(clock)
,time_of_last_received_packet_(ProtoTime::Zero())
,sent_manager_(clock,&con_stats_,this)
,alarm_factory_(alarm_factory)
{
	//pthread_mutex_init(&que_lock,NULL);
    frame_encoder_.set_data_producer(this);
    //to decode ack frame;
	frame_decoder_.set_process_timestamps(true);
    frame_decoder_.set_visitor(this);
    sent_manager_.SetSendAlgorithm(cc);
    std::unique_ptr<SendAlarmDelegate> send_delegate(new SendAlarmDelegate(this));
    send_alarm_=alarm_factory_->CreateAlarm(std::move(send_delegate));
	std::unique_ptr<FastRetransDelegate>  fast_retrans_delegate(new FastRetransDelegate(this));
	fast_retrans_alarm_=alarm_factory_->CreateAlarm(std::move(fast_retrans_delegate));
}
ProtoCon::~ProtoCon(){
    ProtoStream *stream=nullptr;
    while(!streams_.empty()){
        auto it=streams_.begin();
        stream=it->second;
        streams_.erase(it);
        delete stream;
    }
}
void ProtoCon::SetThisCongestionId(uint32_t cid){
    cid_=cid;
    sent_manager_.SetCongestionId(cid);
}
void ProtoCon::SetThisNumEmulatedConnections(int num_connections){
    sent_manager_.SetNumEmulatedConnections(num_connections);
}
void ProtoCon::SetMaxBandwidth(uint32_t bps){
    QuicBandwidth max_rate(QuicBandwidth::FromBitsPerSecond(bps));
    sent_manager_.SetMaxPacingRate(max_rate);
}
void ProtoCon::ProcessUdpPacket(SocketAddress &self,SocketAddress &peer,
                          const ProtoReceivedPacket& packet){
    if(!first_packet_from_peer_){
        if(peer!=peer_){
            DLOG(INFO)<<"wrong peer";
            CHECK(0);
            return;
        }
    }
    
    if(first_packet_from_peer_){
        peer_=peer;
        first_packet_from_peer_=false;
    }
    time_of_last_received_packet_=packet.receipe_time();
    basic::DataReader r(packet.packet(),packet.length());
    ProtoPacketHeader header;
    ProcessPacketHeader(&r,header);
    frame_decoder_.ProcessFrameData(&r,header);
    MaybeSendBulkData();
}
void ProtoCon::Close(uint32_t id){

}
void ProtoCon::Process(){
    if(!packet_writer_){
        DLOG(INFO)<<"set writer first";
        return;
    }
    MaybeSendBulkData();
}
void ProtoCon::SendInitData(){
    CHECK(packet_writer_);
    MaybeSendBulkData();
}
bool ProtoCon::CanWrite(HasRetransmittableData has_retrans){
    if(NO_RETRANSMITTABLE_DATA==has_retrans){
        return true;
    }
    if(send_alarm_->IsSet()){
        return false;
    }
    ProtoTime now=clock_->Now();
    TimeDelta delay=sent_manager_.TimeUntilSend(now);
    if(delay.IsInfinite()){
        send_alarm_->Cancel();
        return false;
    }
    if(!delay.IsZero()){
        if(delay<=release_time_into_future_){
            return true;
        }
        send_alarm_->Update(now+delay,TimeDelta::FromMilliseconds(1));
        return false;
    }
    return true;
}
void ProtoCon::MaybeSendBulkData(){
    if(!fast_retrans_alarm_->IsSet()){
        TimeDelta rto=sent_manager_.GetRetransmissionDelay(0);
        ProtoTime next=clock_->ApproximateNow()+rto;
        fast_retrans_alarm_->Update(next,TimeDelta::FromMilliseconds(1));
    }
    WriteNewData();
}
void ProtoCon::OnCanWriteSession(){
    bool packet_send=SendRetransPending(TT_LOSS_RETRANS);
    if(!packet_send){
        if(waiting_info_.empty()){
            NotifyCanSendToStreams();
        }
        Send();
    }
}
void ProtoCon::WriteNewData(){
    if(!CanWrite(HAS_RETRANSMITTABLE_DATA)){
        return;
    }
    OnCanWriteSession();
    if(!send_alarm_->IsSet()&&CanWrite(HAS_RETRANSMITTABLE_DATA)){
        send_alarm_->Set(clock_->ApproximateNow());
    }
}
void ProtoCon::OnRetransmissionTimeOut(){
    sent_manager_.OnRetransmissionTimeOut();
    SendRetransPending(TT_RTO_RETRANS);
}
void ProtoCon::OnCanWrite(){
    WriteNewData();
}
void ProtoCon::WritevData(uint32_t id,StreamOffset offset,ByteCount len,bool fin){
    struct PacketStream info(id,offset,len,fin);
    waiting_info_.push_back(info);
}
void ProtoCon::OnAckStream(uint32_t id,StreamOffset off,ByteCount len){
    ProtoStream *stream=GetStream(id);
    if(stream){
        DLOG(INFO)<<id<<" "<<off<<" "<<len;
        stream->OnAck(off,len);
    }
}
ProtoStream *ProtoCon::GetOrCreateStream(uint32_t id){
    ProtoStream *stream=GetStream(id);
    if(!stream){
        stream=CreateStream();
    }
    return stream;
}
bool ProtoCon::OnStreamFrame(PacketStream &frame){
    DLOG(INFO)<<"should not be called";
    return true;
}
void ProtoCon::OnError(ProtoFramer* framer){
    DLOG(INFO)<<"frame error";
}
void ProtoCon::OnEcnMarkCount(uint64_t ecn_ce_count){
    sent_manager_.UpdateEcnBytes(ecn_ce_count);
}
bool ProtoCon::OnAckFrameStart(PacketNumber largest_acked,
                                 TimeDelta ack_delay_time){
  DLOG(INFO)<<largest_acked<<" "<<std::to_string(ack_delay_time.ToMilliseconds());
  new_ack_received_time_=clock_->Now();
  sent_manager_.OnAckStart(largest_acked,ack_delay_time,new_ack_received_time_);
  return true;
}
bool ProtoCon::OnAckRange(PacketNumber start, PacketNumber end){
    //DLOG(INFO)<<start<<" "<<end;
    sent_manager_.OnAckRange(start,end);
    return true;
}
bool ProtoCon::OnAckTimestamp(PacketNumber packet_number,
                                ProtoTime timestamp){
	sent_manager_.OnAckTimestamp(packet_number, timestamp);
    return true;
}
bool ProtoCon::OnAckFrameEnd(PacketNumber start){
    DLOG(INFO)<<start;
    sent_manager_.OnAckEnd(new_ack_received_time_);
	TimeDelta rto=sent_manager_.GetRetransmissionDelay(0);
	ProtoTime next=new_ack_received_time_+rto;
	fast_retrans_alarm_->Update(next,TimeDelta::FromMilliseconds(1));
    if(supports_release_time_){
        UpdateReleaseTimeIntoFuture();
    }
    return true;
}
bool ProtoCon::WriteStreamData(uint32_t id,
                               StreamOffset offset,
                               ByteCount len,
                               basic::DataWriter *writer){
    ProtoStream *stream=GetStream(id);
    if(!stream){
        return false;
    }
    return stream->WriteStreamData(offset,len,writer);
}
void ProtoCon::OnFastRetransmit(){
	sent_manager_.FastRetransmit();
    sent_manager_.OnRetransmissionTimeOut();
    TimeDelta rto=sent_manager_.GetRetransmissionDelay(0);
	TimeDelta wall_time=clock_->Now()-ProtoTime::Zero();
	//NS_LOG_INFO(cid_<<" now and rto "<<wall_time.ToMilliseconds()<<" "<<rto.ToMilliseconds());
	ProtoTime next=clock_->Now()+rto;
	fast_retrans_alarm_->Update(next,TimeDelta::FromMilliseconds(1));
    bool send=SendRetransPending(TT_FAST_RETRANS);
    std::cout<<"con time out"<<std::endl;
    CHECK(send);
    fast_retrans_++;
}
ProtoStream *ProtoCon::CreateStream(){
    uint32_t id=stream_id_;
    ProtoStream *stream=new ProtoStream(this,id);
    stream_id_++;
    streams_.insert(std::make_pair(id,stream));
    return stream;
}
ProtoStream *ProtoCon::GetStream(uint32_t id){
    ProtoStream *stream=nullptr;
    std::map<uint32_t,ProtoStream*>::iterator found=streams_.find(id);
    if(found!=streams_.end()){
        stream=found->second;
    }
    return stream;
}
void ProtoCon::NotifyCanSendToStreams(){
    for(auto it=streams_.begin();it!=streams_.end();it++){
        it->second->OnCanWrite();
    }
}
int ProtoCon::Send(){
    if(waiting_info_.empty()){
        return 0;
    }
	if(waiting_info_.size()==0){
		CHECK(0);
	}
	//std::cout<<"wait info size "<<waiting_info_.size()<<std::endl;
	//quite none sense https://blog.csdn.net/chunyunzhe/article/details/79256973
    struct PacketStream info=waiting_info_.front();
    waiting_info_.pop_front();
    char src[1500];
    memset(src,0,sizeof(src));
    ProtoPacketHeader header;
    header.packet_number=AllocSeq();
    basic::DataWriter writer(src,sizeof(src));
    AppendPacketHeader(header,&writer);
    ProtoFrame frame(info);
    uint8_t type=frame_encoder_.GetStreamFrameTypeByte(info,true);
    writer.WriteUInt8(type);
    frame_encoder_.AppendStreamFrame(info,true,&writer);
    SerializedPacket serialized;
    serialized.number=header.packet_number;
    serialized.buf=nullptr;//buf addr is not quite useful;
    //TODO add header info;
    serialized.len=writer.length();
    serialized.retransble_frames.push_back(frame);
    DCHECK(packet_writer_);
    sent_manager_.OnSentPacket(&serialized,PacketNumber(0),HAS_RETRANSMITTABLE_DATA,clock_->Now());
    int available=writer.length();
	if(trace_sent_){
		trace_sent_->OnSent(header.packet_number,clock_->Now());
	}
    packet_writer_->SendTo(src,writer.length(),peer_);
    return available;
}
bool ProtoCon::SendRetransPending(TransType tt){
    bool packet_send=false;
    if(sent_manager_.HasPendingForRetrans()){
        PendingRetransmission pend=sent_manager_.NextPendingRetrans();
        bool has_stream_to_retrans=false;
        for(auto frame_it=pend.retransble_frames.begin();
        frame_it!=pend.retransble_frames.end();frame_it++){
        	if(frame_it->type()==PROTO_FRAME_STREAM){
        		const PacketStream &stream=frame_it->StreamInfo();
        		has_stream_to_retrans=true;
        		Retransmit(stream.stream_id,stream.offset,
            			stream.len,stream.fin,tt);
        	}
        }
        packet_send=true;
    }
    if(!packet_send&&(tt==TT_FAST_RETRANS)){
        //PacketNumber unacked=sent_manager_.GetLeastUnacked();
        //uint64_t una=unacked.ToUint64();
        //NS_LOG_INFO("una "<<una);
        SendStopWaitingFrame();
        packet_send=true;
    }
    return packet_send;
}
void ProtoCon::Retransmit(uint32_t id,StreamOffset off,ByteCount len,bool fin,TransType tt){
    ProtoStream *stream=GetStream(id);
    if(stream){
    DLOG(INFO)<<"retrans "<<off<<" "<<len;
    struct PacketStream info(id,off,len,fin);
    char src[1500];
    memset(src,0,sizeof(src));
    ProtoPacketHeader header;
    PacketNumber seq=AllocSeq();
    header.packet_number=seq;
    basic::DataWriter writer(src,sizeof(src));
    AppendPacketHeader(header,&writer);
    if(TT_LOSS_RETRANS==tt){
    PacketNumber unacked=sent_manager_.GetLeastUnacked();
    DLOG(INFO)<<"send stop waiting "<<seq<<" "<<unacked;
    writer.WriteUInt8(PROTO_FRAME_STOP_WAITING);
    frame_encoder_.AppendStopWaitingFrame(header,unacked,&writer);
    }
    uint8_t type=frame_encoder_.GetStreamFrameTypeByte(info,true);
    writer.WriteUInt8(type);
    frame_encoder_.AppendStreamFrame(info,true,&writer);
    ProtoFrame frame(info);
    SerializedPacket serialized;
    serialized.number=header.packet_number;
    serialized.buf=nullptr;//buf addr is not quite useful;
    serialized.len=writer.length();
    serialized.retransble_frames.push_back(frame);
    sent_manager_.OnSentPacket(&serialized,QuicPacketNumber(0),HAS_RETRANSMITTABLE_DATA,clock_->Now());
    packet_writer_->SendTo(src,writer.length(),peer_);
    }
}
void ProtoCon::SendStopWaitingFrame(){
    char src[1500];
    memset(src,0,sizeof(src));
    ProtoPacketHeader header;
    PacketNumber seq=AllocSeq();
    header.packet_number=seq;
    basic::DataWriter writer(src,sizeof(src));
    AppendPacketHeader(header,&writer);
    PacketNumber unacked=sent_manager_.GetLeastUnacked();
    DLOG(INFO)<<"only send stop waiting "<<seq<<" "<<unacked;
    writer.WriteUInt8(PROTO_FRAME_STOP_WAITING);
    frame_encoder_.AppendStopWaitingFrame(header,unacked,&writer);
    ProtoFrame frame(PROTO_FRAME_STOP_WAITING);
    SerializedPacket serialized;
    serialized.number=header.packet_number;
    serialized.buf=nullptr;//buf addr is not quite useful;
    serialized.len=writer.length();
    serialized.retransble_frames.push_back(frame);
    sent_manager_.OnSentPacket(&serialized,QuicPacketNumber(0),NO_RETRANSMITTABLE_DATA,clock_->Now());
    packet_writer_->SendTo(src,writer.length(),peer_);
}
void ProtoCon::UpdateReleaseTimeIntoFuture(){
  release_time_into_future_ = std::max(
      TimeDelta::FromMilliseconds(kMinReleaseTimeIntoFutureMs),
      std::min(
          TimeDelta::FromMilliseconds(
              GetQuicFlag(FLAGS_quic_max_pace_time_into_future_ms)),
          sent_manager_.GetRttStats()->SmoothedOrInitialRtt() *
              GetQuicFlag(FLAGS_quic_pace_time_into_future_srtt_fraction)));
}
}//namespace dqc;
