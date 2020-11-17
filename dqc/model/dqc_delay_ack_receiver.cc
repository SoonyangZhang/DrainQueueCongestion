#include "ns3/dqc_delay_ack_receiver.h"
#include "ns3/log.h"
#include "ns3/time_tag.h"
#include "byte_codec.h"
using namespace dqc;
namespace ns3{
NS_LOG_COMPONENT_DEFINE("dqcdelayackreceiver");
const uint64_t mMinReceivedBeforeAckDecimation = 100;

// Wait for up to 10 retransmittable packets before sending an ack.
const uint64_t mMaxRetransmittablePacketsBeforeAck = 10;
// Maximum delayed ack time, in ms.
const int64_t mMaxDelayedAckTimeMs = 25;
// TCP RFC calls for 1 second RTO however Linux differs from this default and
// define the minimum RTO to 200ms, we will use the same until we have data to
// support a higher or lower value.
static const int64_t mMinRetransmissionTimeMs = 200;
class AckDelegate:public dqc::Alarm::Delegate{
public:
	AckDelegate(DqcDelayAckReceiver *receiver):receiver_(receiver){}
	~AckDelegate(){}
    void OnAlarm() override{
    	receiver_->SendAckFrame();
    }
private:
	DqcDelayAckReceiver *receiver_{nullptr};
};
DqcDelayAckReceiver::DqcDelayAckReceiver()
:m_alarmFactory(new ProcessAlarmFactory(&m_timeDriver)){
	m_frameDecoder.set_visitor(this);
	std::unique_ptr<Alarm::Delegate> ack_delegate(new AckDelegate(this));
	m_ackAlarm=m_alarmFactory->CreateAlarm(std::move(ack_delegate));
}
void DqcDelayAckReceiver::Bind(uint16_t port){
    if (m_socket== NULL) {
        m_socket = Socket::CreateSocket (GetNode (),UdpSocketFactory::GetTypeId ());
        auto local = InetSocketAddress{Ipv4Address::GetAny (), port};
        auto res = m_socket->Bind (local);
        NS_ASSERT (res == 0);
    }
    m_bindPort=port;
    m_socket->SetRecvCallback (MakeCallback(&DqcDelayAckReceiver::RecvPacket,this));
}
InetSocketAddress DqcDelayAckReceiver::GetLocalAddress(){
    Ptr<Node> node=GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    Ipv4Address local_ip = ipv4->GetAddress (1, 0).GetLocal ();
	return InetSocketAddress{local_ip,m_bindPort};
}
bool DqcDelayAckReceiver::OnStreamFrame(dqc::PacketStream &frame){
    if(m_recvInterval.Empty()){
    	m_recvInterval.Add(frame.offset,frame.offset+frame.len);
    }else{
        if(m_recvInterval.IsDisjoint(frame.offset,frame.offset+frame.len)){
            m_recvInterval.Add(frame.offset,frame.offset+frame.len);
        }else{
            NS_LOG_INFO("redundancy "<<frame.offset);
        }
    }
	return true;
}
void DqcDelayAckReceiver::OnError(dqc::ProtoFramer* framer){

}
bool DqcDelayAckReceiver::OnAckFrameStart(dqc::PacketNumber largest_acked,
                     dqc::TimeDelta ack_delay_time){
	return true;
}
bool DqcDelayAckReceiver::OnAckRange(dqc::PacketNumber start,
                dqc::PacketNumber end){
	return true;
}
bool DqcDelayAckReceiver::OnAckTimestamp(dqc::PacketNumber packet_number,
		dqc::ProtoTime timestamp){
	return true;
}
bool DqcDelayAckReceiver::OnAckFrameEnd(dqc::PacketNumber start){
	return true;
}
bool DqcDelayAckReceiver::OnStopWaitingFrame(const dqc::PacketNumber least_unacked){
    NS_LOG_INFO("stop waiting "<<least_unacked.ToUint64());
    m_recvManager.DontWaitForPacketsBefore(least_unacked);
	return true;
}
void DqcDelayAckReceiver::StartApplication(){
}
void DqcDelayAckReceiver::StopApplication(){
}
void DqcDelayAckReceiver::MaybeSendAck(){
    bool should_send = false;
    if (m_received< mMinReceivedBeforeAckDecimation)
    {
        should_send = true;
    }
    else
    {
        if (m_num_packets_received_since_last_ack_sent >= mMaxRetransmittablePacketsBeforeAck)
        {
            should_send = true;
        }
        else if (!m_ackAlarm->IsSet())
        {
            uint64_t ack_delay = std::min(mMaxDelayedAckTimeMs, mMinRetransmissionTimeMs / 2);
            ProtoTime next=m_clock.Now()+TimeDelta::FromMilliseconds(ack_delay);
            m_ackAlarm->Update(next,TimeDelta::FromMilliseconds(1));
        }
    }
    if (should_send)
    {
    	SendAckFrame();
    }
}
void DqcDelayAckReceiver::SendAckFrame(){
	dqc::ProtoTime now=m_clock.Now();
	const AckFrame &ack_frame=m_recvManager.GetUpdateAckFrame(now);
    char buf[1500];
    basic::DataWriter w(buf,1500);
    ProtoPacketHeader header;
    header.packet_number=AllocSeq();
    AppendPacketHeader(header,&w);
    m_frameEncoder.AppendAckFrameAndTypeByte(ack_frame,&w);
    Ptr<Packet> p=Create<Packet>((uint8_t*)buf,w.length());
	SendToNetwork(p);
	m_num_packets_received_since_last_ack_sent=0;
}
void DqcDelayAckReceiver::RecvPacket(Ptr<Socket> socket){
	Address remoteAddr;
	auto packet = socket->RecvFrom (remoteAddr);
	if(!m_knowPeer){
        m_peerIp= InetSocketAddress::ConvertFrom (remoteAddr).GetIpv4 ();
	    m_peerPort= InetSocketAddress::ConvertFrom (remoteAddr).GetPort ();
		m_knowPeer=true;
	}
	if(!m_timerTriggered){
		m_processTimer=Simulator::ScheduleNow(&DqcDelayAckReceiver::Process,this);
		m_timerTriggered=true;
	}
	ProtoTime now=m_clock.Now();
	uint32_t recv=packet->GetSize ();
	TimeTag tag;
	packet->PeekPacketTag (tag);
	uint32_t owd=Simulator::Now().GetMilliSeconds()-tag.GetSentTime();
	uint8_t buf[1500]={'\0'};
	packet->CopyData(buf,recv);
    basic::DataReader r((char*)buf,recv);
    ProtoPacketHeader header;
    ProcessPacketHeader(&r,header);
    PacketNumber seq=header.packet_number;
    m_recvManager.RecordPacketReceived(seq,now);
    m_frameDecoder.ProcessFrameData(&r,header);
    m_received++;
    m_num_packets_received_since_last_ack_sent++;
	if(!m_traceOwdCb.IsNull()){
		m_traceOwdCb((uint32_t)seq.ToUint64(),owd);
	}
    MaybeSendAck();
}
void DqcDelayAckReceiver::SendToNetwork(Ptr<Packet> p){
    m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
}
void DqcDelayAckReceiver::Process(){
	   if(m_processTimer.IsExpired()){
	    	ProtoTime now=m_clock.Now();
	    	m_timeDriver.HeartBeat(now);
	        Time next=MilliSeconds(m_ProcessInteval);
	        m_processTimer=Simulator::Schedule(next,&DqcDelayAckReceiver::Process,this);
	    }
}
}
