#include "send_receive.h"
#include "logging.h"
#include "random.h"
namespace dqc{
const char *ip="127.0.0.1";
const uint16_t send_port=1234;
const uint16_t recv_port=4321;
static SystemClock local_clock;
/*
class FakeReceiver:public Socket,ProtoFrameVisitor{
public:
    FakeReceiver(){
        frame_decoder_.set_visitor(this);
    }
    virtual bool OnStreamFrame(PacketStream &frame) override{
        //std::string str(frame.data_buffer,frame.len);
        DLOG(INFO)<<"recv "<<frame.offset<<" "<<frame.len;
        return true;
    }
    virtual void OnError(ProtoFramer* framer) override{
    }
    virtual bool OnAckFrameStart(PacketNumber largest_acked,
                                 TimeDelta ack_delay_time) override{
        return true;
                                 }
    virtual bool OnAckRange(PacketNumber start,
                            PacketNumber end) override{
                                return true;
                            }
    virtual bool OnAckTimestamp(PacketNumber packet_number,
                                ProtoTime timestamp) override{
                                    return true;
                                }
    virtual bool OnAckFrameEnd(PacketNumber start) override{
        return true;
    }
    virtual int SendTo(const char*buf,size_t size,SocketAddress &dst) override{
        std::unique_ptr<char> data(new char[size]);
        memcpy(data.get(),buf,size);
        basic::DataReader r(data.get(),size);
        ProtoPacketHeader header;
        ProcessPacketHeader(&r,header);
        DLOG(INFO)<<"seq "<<header.packet_number;
        frame_decoder_.ProcessFrameData(&r,header);
        return 0;
    }
private:
    ProtoFramer frame_decoder_;
    SocketAddress local_;
    FakeAckFrameReceive *feed_ack_{nullptr};
};
SimulateSender::SimulateSender()
:connection_(&local_clock){
    sock_=new UdpSocket();
    sock_->Bind(ip,send_port);
    connection_.set_packet_writer(sock_);
    stream_=connection_.GetOrCreateStream(stream_id_);
}
SimulateSender::~SimulateSender(){
    if(sock_){
        delete sock_;
    }
}
void SimulateSender::Process(){
    connection_.Process(stream_id_);
}
void SimulateSender::set_packet_writer(Socket *sock){
    connection_.set_packet_writer(sock);
}
void SimulateSender::DataGenerator(int times){
    char data[1500];
    int i=0;
    for (i=0;i<1500;i++){
        data[i]=RandomLetter::Instance()->GetLetter();
    }
    std::string piece(data,1500);
    for(i=0;i<times;i++){
        stream_->WriteDataToBuffer(piece);
    }
}
void SimulateSender::OnPeerData(SocketAddress &peer,char *data,
                    int len,ProtoTime &recipt_time){
    ProtoReceivedPacket packet(data,len,recipt_time);
    connection_.ProcessUdpPacket(local_,peer,packet);
}
SimulateReceiver::SimulateReceiver(){
    frame_decoder_.set_visitor(this);
}
SimulateReceiver::~SimulateReceiver(){}
bool SimulateReceiver::OnStreamFrame(PacketStream &frame){
    DLOG(INFO)<<"stream "<<frame.offset;
    return true;
}
void SimulateReceiver::OnError(ProtoFramer* framer){}
bool SimulateReceiver::OnAckFrameStart(PacketNumber largest_acked,
                               TimeDelta ack_delay_time){
    DLOG(INFO)<<"SNC";
    return false;
}
bool SimulateReceiver::OnAckRange(PacketNumber start,
                          PacketNumber end){
    DLOG(INFO)<<"SNC";
    return false;
}
bool SimulateReceiver::OnAckTimestamp(PacketNumber packet_number,
                              ProtoTime timestamp){
    DLOG(INFO)<<"SNC";
    return false;
}
bool SimulateReceiver::OnAckFrameEnd(PacketNumber start){
    DLOG(INFO)<<"SNC";
    return false;
}
bool SimulateReceiver::OnStopWaitingFrame(const PacketNumber least_unacked){
    DLOG(INFO)<<"stop waiting "<<least_unacked.ToUint64();
    received_packet_manager_.DontWaitForPacketsBefore(least_unacked);
    return true;
}
//simulate recv data from peer;
int SimulateReceiver::SendTo(const char*buf,size_t size,SocketAddress &dst){
    std::unique_ptr<char> data(new char[size]);
    memcpy(data.get(),buf,size);
    basic::DataReader r(data.get(),size);
    ProtoPacketHeader header;
    ProcessPacketHeader(&r,header);
    PacketNumber seq=header.packet_number;
    //DLOG(INFO)<<"sendrecv seq "<<seq;
    bool drop=false;
    if(2==seq.ToUint64()||5==seq.ToUint64()||9==seq.ToUint64()){
        drop=true;
    }
    ProtoTime receive_time=ProtoTime::Zero();
    if(!drop){
    receive_time=base_time_+offset_*count_;
    // to prevent process stop_waiting first;
    received_packet_manager_.RecordPacketReceived(seq,receive_time);
    frame_decoder_.ProcessFrameData(&r,header);
    }
    count_++;
    if(!drop){
        SendAckFrame(receive_time);
    }
    return size;
}
void SimulateReceiver::SendAckFrame(ProtoTime now){
    ProtoTime ack_delay=now+one_way_delay_;
    const AckFrame &ack_frame=received_packet_manager_.GetUpdateAckFrame(now);
    char wbuf[1500];
    basic::DataWriter writer(wbuf,1500,basic::NETWORK_ORDER);
    ProtoPacketHeader header;
    header.packet_number=AllocSeq();
    AppendPacketHeader(header,&writer);
    framer_encoder_.AppendAckFrameAndTypeByte(ack_frame,&writer);
    if(feed_ack_){
        feed_ack_->OnPeerData(local_,wbuf,writer.length(),ack_delay);
    }
}
void simu_send_receiver_test(){
    SocketAddress send_addr(ip,send_port);
    SocketAddress recv_addr(ip,recv_port);
    SimulateSender sender;
    sender.set_address(send_addr);
    SimulateReceiver receiver;
    receiver.set_address(recv_addr);
    receiver.set_ack_sink(&sender);
    sender.set_packet_writer(&receiver);
    sender.DataGenerator(10);
    for(int i=0;i<15;i++){
        sender.Process();
    }
    AbstractAlloc *alloc=AbstractAlloc::Instance();
    alloc->CheckMemLeak();
}*/
Sender::Sender(ProtoClock *clock)
:clock_(clock)
,alarm_factory_(new ProcessAlarmFactory(&time_driver_))
,connection_(clock,alarm_factory_.get(),kBBR){
    const TimeDelta delta=connection_.GetRetransmissionDelay();
    rto_=clock_->Now()+delta;
    DLOG(INFO)<<delta;
}
Sender::~Sender(){
    if(fd_){
        delete fd_;
    }
}
void Sender::Bind(const char *ip,uint16_t port){
    local_=SocketAddress(ip,port);
    fd_=new UdpSocket();
    fd_->Bind(ip,port);
    connection_.set_packet_writer(fd_);
    stream_=connection_.GetOrCreateStream(stream_id_);
    stream_->set_stream_vistor(this);
}
void Sender::set_peer(SocketAddress &peer){
    connection_.set_peer(peer);
}
void Sender::Process(){
    if(!running_){
        return;
    }
    ProtoTime now=clock_->Now();
    time_driver_.HeartBeat(now);
    bool rto_time_out=false;
    if(test_rto_flag_&&now>rto_){
        connection_.OnRetransmissionTimeOut();
        rto_time_out=true;
    }
    connection_.Process();

    char buf[1500];
    int recv=0;
    SocketAddress from;
    recv=fd_->RecvFrom(buf,1500,from);
    if(recv>0){
        ProtoTime now=clock_->Now();
        ProtoReceivedPacket packet(buf,recv,now);
        connection_.ProcessUdpPacket(local_,from,packet);
    }
    if(rto_time_out){
        running_=false;
    }
}
void Sender::DataGenerator(int times){
    if(!stream_){
        return ;
    }
    char data[1500];
    int i=0;
    for (i=0;i<1500;i++){
        data[i]=RandomLetter::Instance()->GetLetter();
    }
    std::string piece(data,1500);
    bool success=false;
    for(i=0;i<times;i++){
        if(data_generated_index_>total_generated_){
            break;
        }
        success=stream_->WriteDataToBuffer(piece);
        if(!success){
            break;
        }
        data_generated_index_++;
    }
}
Receiver::Receiver(ProtoClock *clock)
:clock_(clock){
    frame_decoder_.set_visitor(this);
}
Receiver::~Receiver(){
    if(!recv_interval_.Empty()){
        IntervalSet<StreamOffset>::iterator it=recv_interval_.begin();
        for(;it!=recv_interval_.end();it++){
            DLOG(INFO)<<it->Min()<<" "<<it->Max();
        }
    }
    if(fd_){
        delete fd_;
    }
}
void Receiver::Bind(const char *ip,uint16_t port){
    local_=SocketAddress(ip,port);
    fd_=new UdpSocket();
    fd_->Bind(ip,port);
}
void Receiver::Process(){
    char buf[1500];
    int recv=0;
    SocketAddress from;
    recv=fd_->RecvFrom(buf,1500,from);
    if(nerver_feed_ack_){
        return;
    }
    if(recv>0){
        peer_=from;
        ProtoTime now=clock_->Now();
        basic::DataReader r(buf,recv);
        ProtoPacketHeader header;
        ProcessPacketHeader(&r,header);
        PacketNumber seq=header.packet_number;
        bool drop=false;
        if(2==seq.ToUint64()||5==seq.ToUint64()||9==seq.ToUint64()){
            drop=true;
        }
        if(!drop){
            received_packet_manager_.RecordPacketReceived(seq,now);
            frame_decoder_.ProcessFrameData(&r,header);
            SendAckFrame(now);
        }
    }
}
bool Receiver::OnStreamFrame(PacketStream &frame){
    //DLOG(INFO)<<"recv "<<frame.offset<<" "<<frame.len;
    if(recv_interval_.Empty()){
        recv_interval_.Add(frame.offset,frame.offset+frame.len);
    }else{
        if(recv_interval_.IsDisjoint(frame.offset,frame.offset+frame.len)){
            recv_interval_.Add(frame.offset,frame.offset+frame.len);
        }else{
            DLOG(INFO)<<"redundancy "<<frame.offset;
        }
    }
    return true;
}
void Receiver::OnError(ProtoFramer* framer){

}
bool Receiver::OnAckFrameStart(PacketNumber largest_acked,
                               TimeDelta ack_delay_time){
        return true;
}
bool Receiver::OnAckRange(PacketNumber start,
                          PacketNumber end){
    return true;
}
bool Receiver::OnAckTimestamp(PacketNumber packet_number,
                              ProtoTime timestamp){
    return true;
}
bool Receiver::OnAckFrameEnd(PacketNumber start){
    return true;
}
bool Receiver::OnStopWaitingFrame(const PacketNumber least_unacked){
    DLOG(INFO)<<"stop waiting "<<least_unacked.ToUint64();
    received_packet_manager_.DontWaitForPacketsBefore(least_unacked);
    return true;
}
void Receiver::SendAckFrame(ProtoTime now){
    const AckFrame &ack_frame=received_packet_manager_.GetUpdateAckFrame(now);
    char buf[1500];
    basic::DataWriter writer(buf,1500,basic::NETWORK_ORDER);
    ProtoPacketHeader header;
    header.packet_number=AllocSeq();
    AppendPacketHeader(header,&writer);
    framer_encoder_.AppendAckFrameAndTypeByte(ack_frame,&writer);
    fd_->SendTo(buf,writer.length(),peer_);
}
}
