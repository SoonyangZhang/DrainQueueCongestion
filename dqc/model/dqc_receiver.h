#pragma once
#include "ns3/simulator.h"
#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/callback.h"
#include "ns3/dqc_clock.h"
#include "ns3/proto_types.h"
#include "ns3/interval.h"
#include "ns3/packet_number.h"
#include "ns3/received_packet_manager.h"
#include "ns3/proto_framer.h"
#include "ns3/process_alarm_factory.h"
namespace ns3{
class DqcReceiver:public Application,
public dqc::ProtoFrameVisitor{
public:
    DqcReceiver(uint32_t goodput_granularity=1000);
    ~DqcReceiver();
	typedef Callback<void,uint32_t,uint32_t,uint32_t> TraceOwd;
	void SetOwdTraceFuc(TraceOwd cb){
		m_traceOwdCb=cb;
	}
	typedef Callback<void,uint64_t,uint64_t,uint64_t,uint64_t,float> TraceStats;
	void SetStatsTraceFuc(TraceStats cb){
		m_traceStatsCb=cb;
	}
    typedef Callback<void,uint32_t> TraceGoodput;
    void SetGoodputTraceFuc(TraceGoodput cb){
        m_traceGoodputCb=cb;
    }
	void Bind(uint16_t port);
	InetSocketAddress GetLocalAddress();
    bool OnStreamFrame(dqc::PacketStream &frame) override;
    void OnError(dqc::ProtoFramer* framer) override;
    void OnEcnMarkCount(uint64_t ecn_ce_count) override;
    bool OnAckFrameStart(dqc::PacketNumber largest_acked,
                         dqc::TimeDelta ack_delay_time) override;
    bool OnAckRange(dqc::PacketNumber start,
                    dqc::PacketNumber end) override;
    bool OnAckTimestamp(dqc::PacketNumber packet_number,
    		dqc::ProtoTime timestamp) override;
    bool OnAckFrameEnd(dqc::PacketNumber start) override;
    bool OnStopWaitingFrame(const dqc::PacketNumber least_unacked) override;
    void SendAckFrame();
private:
	virtual void StartApplication() override;
	virtual void StopApplication() override;
    dqc::PacketNumber AllocSeq(){
        return m_seq++;
    }
	void RecvPacket(Ptr<Socket> socket);
    void RecvEcnCallback(uint8_t ecn);
	void SendToNetwork(Ptr<Packet> p);
    void CalGoodput(uint32_t now);
    uint32_t m_goodputGra=1000;
	bool m_running{true};
    bool m_knowPeer{false};   
    Ipv4Address m_peerIp;
    uint16_t m_peerPort;
    uint16_t m_bindPort;
    Ptr<Socket> m_socket;
    DqcSimuClock m_clock;
    dqc::PacketNumber m_seq{1};
	dqc::PacketNumber m_largestSeq;
    dqc::ReceivdPacketManager m_recvManager;
    dqc::ProtoFramer m_frameDecoder;
    dqc::ProtoFramer m_frameEncoder;
    IntervalSet<dqc::StreamOffset> m_recvInterval;
	TraceOwd m_traceOwdCb;
    TraceStats m_traceStatsCb;
    TraceGoodput m_traceGoodputCb;
    bool m_ecn_flag{false};
    uint64_t m_recvCounter=0;
    uint64_t m_recvBytes=0;
    uint64_t m_timeFirstPacket=0;
    uint32_t m_timeLastPacket=0;
    uint64_t m_sumOwd=0;
    uint32_t m_lastCalGoodTime=0;
    uint32_t m_lastGoodRecv=0;
};    
}
