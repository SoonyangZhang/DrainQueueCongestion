#include <string>
#include "ns3/simulator.h"
#include "ns3/dqc_sender.h"
#include "ns3/log.h"
#include "ns3/time_tag.h"
#include "byte_codec.h"
#include "proto_utils.h"
using namespace dqc;
namespace ns3{
NS_LOG_COMPONENT_DEFINE("dqcsender");
using namespace dqc;
// in order to get the ip addr of node
void ConvertIp32(uint32_t ip,std::string &addr){
 uint8_t first=(ip&0xff000000)>>24;
 uint8_t second=(ip&0x00ff0000)>>16;
 uint8_t third=(ip&0x0000ff00)>>8;
 uint8_t fourth=(ip&0x000000ff);
 std::string dot=std::string(".");
 addr=std::to_string(first)+dot+std::to_string(second)+dot+std::to_string(third)+dot+std::to_string(fourth);
}
int FakePackeWriter::SendTo(const char*buf,size_t size,dqc::SocketAddress &dst){
    Ptr<Packet> p=Create<Packet>((uint8_t*)buf,size);
	m_sender->SendToNetwork(p);
	return size;
}
/*kQueueLimit kShadow kBBRv2 kBBR kPOTEN kCubicBytes*/
DqcSender::DqcSender(bool ecn):DqcSender(kBBR,ecn){}
DqcSender::DqcSender(dqc::CongestionControlType cc_type,bool ecn,bool engine_time)
:m_writer(this)
,m_alarmFactory(new ProcessAlarmFactory(&m_timeDriver))
,m_connection(&m_clock,m_alarmFactory.get(), cc_type){
	m_ecn=ecn;
    m_enableEngineTimer=engine_time;
}
void DqcSender::SetBwTraceFuc(TraceBandwidth cb){
	m_traceBwCb=cb;
	if(m_traceBwCb.IsNull()){
		NS_LOG_INFO("bw trace is null");
		abort();
	}
}
void DqcSender::SetMaxBandwidth(uint32_t bps){
    m_connection.SetMaxBandwidth(bps);
}
void DqcSender::SetSentSeqTraceFuc(TraceSentSeq cb){
	m_traceSentSeqCb=cb;
	if(m_traceSentSeqCb.IsNull()){
		NS_LOG_INFO("bw trace is null");
		abort();
	}
}
void DqcSender::SetTraceLossPacketDelay(TraceLossPacketDelay cb){
    m_traceLossDelay=cb;
    m_connection.SetTraceLossPacketDelay([this](PacketNumber seq,uint32_t rtt){
        OnPacketLossInfo(seq,rtt);
    });
}
void DqcSender::SetTraceOwdAtSender(TraceOwdAtSender cb){
	 m_traceOwd=cb;
}
void DqcSender::OnPacketLossInfo(dqc::PacketNumber seq,uint32_t rtt){
    if(!m_traceLossDelay.IsNull()){
        int32_t num=(int32_t)seq.ToUint64();
        m_traceLossDelay(num,rtt);
    }
}
void DqcSender::Bind(uint16_t port){
    if (m_socket== NULL) {
        m_socket = Socket::CreateSocket (GetNode (),UdpSocketFactory::GetTypeId ());
        auto local = InetSocketAddress{Ipv4Address::GetAny (), port};
        auto res = m_socket->Bind (local);
        NS_ASSERT (res == 0);
    }
    m_bindPort=port;
    m_socket->SetRecvCallback (MakeCallback(&DqcSender::RecvPacket,this));
    if(m_ecn){
	m_socket->SetIpTos(0x01);
    }
    m_connection.set_packet_writer(&m_writer);
	m_connection.SetTraceSentSeq(this);
    m_stream=m_connection.GetOrCreateStream(m_streamId);
    m_stream->set_stream_vistor(this);
	SendPacketManager *sent_manager=m_connection.GetSentPacketManager();
	RttStats* rtt_stats=sent_manager->GetRttStats();
	rtt_stats->UpdateRtt(TimeDelta::FromMilliseconds(100),TimeDelta::FromMilliseconds(0),ProtoTime::Zero());
	NS_LOG_INFO(rtt_stats->smoothed_rtt().ToMicroseconds());
}
InetSocketAddress DqcSender::GetLocalAddress(){
    Ptr<Node> node=GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    Ipv4Address local_ip = ipv4->GetAddress (1, 0).GetLocal ();
	return InetSocketAddress{local_ip,m_bindPort};
}
void DqcSender::ConfigurePeer(Ipv4Address addr,uint16_t port){
	m_peerIp=addr;
	m_peerPort=port;
	std::string ip_str;
	ConvertIp32(addr.Get(),ip_str);
	m_remote=SocketAddress(ip_str,port);
	NS_LOG_INFO(m_remote.ToString());
}
void DqcSender::DataGenerator(int times){
    if(!m_stream){
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
        if(m_pakcetLimit&&(m_packetGenerated>m_packetAllowed)){
            break;
        }
        success=m_stream->WriteDataToBuffer(piece);
        if(!success){
            break;
        }
        m_packetGenerated++;
    }
}
void DqcSender::StartApplication(){
    m_running=true;
    if(m_enableEngineTimer){
        m_connection.SendInitData();
        UpdateEngineEvent();        
    }else{
        m_processTimer=Simulator::ScheduleNow(&DqcSender::Process,this);
    }

}
void DqcSender::StopApplication(){
    m_running=false;
	m_processTimer.Cancel();
	m_engineTimer.Cancel();
    m_sinks.clear();
}
void DqcSender::RecvPacket(Ptr<Socket> socket){
    if(!m_running){return;}
	Address remoteAddr;
	auto p = socket->RecvFrom (remoteAddr);
	uint32_t recv=p->GetSize ();
    ProtoTime now=m_clock.Now();
    uint8_t buf[1500]={'\0'};
    p->CopyData(buf,recv);
    ProtoReceivedPacket packet((char*)buf,recv,now);
    m_connection.ProcessUdpPacket(m_self,m_remote,packet);
    PostProceeAfterReceiveFromPeer();
    if(m_enableEngineTimer){
        //when acked, new packet may be allowed to be sent out,so update callback timer;
        UpdateEngineEvent();        
    }
	
}
void DqcSender::SendToNetwork(Ptr<Packet> p){
	uint32_t ms=Simulator::Now().GetMilliSeconds();
	m_lastSentTs=ms;
	TimeTag tag;
    tag.SetSentTime (ms);
	p->AddPacketTag (tag);
	if(!m_traceBwCb.IsNull()){
		QuicBandwidth send_bw=m_connection.EstimatedBandwidth();
		m_traceBwCb((int32_t)send_bw.ToKBitsPerSecond());
		//NS_LOG_INFO("bw "<<std::to_string((int32_t)send_bw.ToKBitsPerSecond()));
	}
    m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
}
void DqcSender::OnSent(dqc::PacketNumber seq,dqc::ProtoTime sent_ts) {
	if(!m_traceSentSeqCb.IsNull()){
		int32_t sent=(int32_t)seq.ToUint64();
		m_traceSentSeqCb(sent);
	}
}
void DqcSender::Process(){
    if(!m_running){return ;}
    if(m_processTimer.IsExpired()){
    	CheckNoPacketOut();
    	ProtoTime now=m_clock.Now();
    	m_timeDriver.HeartBeat(now);
    	m_connection.Process();
        Time next=MicroSeconds(m_packetInteval);
        m_processTimer=Simulator::Schedule(next,&DqcSender::Process,this);
    }
}
void DqcSender::CheckNoPacketOut(){
    if(!m_running){return ;}
	int64_t now_ms=Simulator::Now().GetMilliSeconds();
	if(m_lastSentTs!=0){
		if((now_ms-m_lastSentTs)>5000){
			SendPacketManager *sent_manager=m_connection.GetSentPacketManager();
			int32_t largest_sent=(int32_t)(m_connection.GetMaxSentSeq().ToUint64());
			int32_t largest_acked=(int32_t)(sent_manager->largest_acked().ToUint64());
			int buffer=m_stream->BufferedBytes();
			ByteCount in_flight=0;
			ByteCount cwnd=0;
			sent_manager->InFlight(&in_flight,&cwnd);
			NS_LOG_ERROR(__FILE__<<std::to_string(largest_sent)<<" "<<std::to_string(largest_acked));
			NS_LOG_ERROR(std::to_string(buffer)
			<<" "<<std::to_string(m_stream->get_send_buffer_len())
			<<" "<<sent_manager->CheckCanSend()
			<<" "<<std::to_string(in_flight)
			<<" cwnd "<<std::to_string(cwnd)
			<<"fres "<<m_connection.GetFastRetrans()
			);
		}
	}
}
void DqcSender::EngineEvent(){
    if(!m_running){return ;}
	if(m_engineTimer.IsExpired()){
		UpdateEngineEvent();
		CheckNoPacketOut();
	}
}
void DqcSender::UpdateEngineEvent(){
    ProtoTime now=m_clock.Now();
    m_timeDriver.ExecuteCallback(now);
    ProtoTime nextEventTime=m_timeDriver.PeekNextEventTime();
	if(!m_engineTimer.IsExpired()){m_engineTimer.Cancel();}
    CHECK(nextEventTime!=ProtoTime::Infinite());
    CHECK(nextEventTime>=now);
    Time next=MicroSeconds((nextEventTime-now).ToMicroseconds());
    m_engineTimer=Simulator::Schedule(next,&DqcSender::EngineEvent,this);
}
void DqcSender::SetSenderId(uint32_t id){
    if(m_id!=0||id==0){
        return;
    }
    m_id=id;
}
void DqcSender::RegisterOnewayDelaySink(OneWayDelaySink *sink){
    bool existed=false;
    for(auto it=m_sinks.begin();it!=m_sinks.end();it++){
        if(sink==(*it)){
            existed=true;
            break;
        }
    }
    if(!existed){
        m_sinks.push_back(sink);
    }
}
void DqcSender::SetCongestionId(uint32_t cid){
    m_connection.SetThisCongestionId(cid);
}
void DqcSender::SetNumEmulatedConnections(int num_connections){
    m_connection.SetThisNumEmulatedConnections(num_connections);
}
void DqcSender::PostProceeAfterReceiveFromPeer(){
    std::pair<PacketNumber,TimeDelta> delay=m_connection.GetOneWayDelayInfo();
    bool newSample=false;
    if(m_lastAckedSeq==PacketNumber(0)){
        newSample=true;
    }else{
        if(delay.first>m_lastAckedSeq){
            newSample=true;
        }
    }
    if(newSample){
        m_lastAckedSeq=delay.first;
        uint32_t seq=(uint32_t)m_lastAckedSeq.ToUint64();
        uint32_t owd=(uint32_t)delay.second.ToMilliseconds();
        if(!m_traceOwd.IsNull()){
            m_traceOwd(seq,owd);
        }
        for(auto it=m_sinks.begin();it!=m_sinks.end();it++){
            (*it)->OnOneWayDelaySample(m_id,seq,owd);
        }
    }
}
}
