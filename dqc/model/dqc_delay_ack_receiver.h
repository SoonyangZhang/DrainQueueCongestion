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
#include "ns3/process_alarm_factory.h"
#include "ns3/received_packet_manager.h"
#include "ns3/proto_framer.h"
namespace ns3{
class DqcDelayAckReceiver:public Application,
public dqc::ProtoFrameVisitor{
public:
    DqcDelayAckReceiver();
    ~DqcDelayAckReceiver(){}
	typedef Callback<void,uint32_t,uint32_t> TraceOwd;
	void SetOwdTraceFuc(TraceOwd cb){
		m_traceOwdCb=cb;
	}
	void Bind(uint16_t port);
	InetSocketAddress GetLocalAddress();
    bool OnStreamFrame(dqc::PacketStream &frame) override;
    void OnError(dqc::ProtoFramer* framer) override;
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
	void MaybeSendAck();
    dqc::PacketNumber AllocSeq(){
        return m_seq++;
    }
	void RecvPacket(Ptr<Socket> socket);
	void SendToNetwork(Ptr<Packet> p);
	void Process();
    bool m_knowPeer{false};
    Ipv4Address m_peerIp;
    uint16_t m_peerPort;
    uint16_t m_bindPort;
    Ptr<Socket> m_socket;
    DqcSimuClock m_clock;
    dqc::PacketNumber m_seq{1};
    dqc::ReceivdPacketManager m_recvManager;
    dqc::ProtoFramer m_frameDecoder;
    dqc::ProtoFramer m_frameEncoder;
    IntervalSet<dqc::StreamOffset> m_recvInterval;
    TraceOwd m_traceOwdCb;
    EventId m_processTimer;
    bool m_timerTriggered{false};
    dqc::MainEngine m_timeDriver;
    std::shared_ptr<dqc::AlarmFactory> m_alarmFactory;
    std::shared_ptr<dqc::Alarm> m_ackAlarm;
    int64_t m_ProcessInteval{1};//1 ms
    uint64_t m_received{0};
    uint64_t m_num_packets_received_since_last_ack_sent{0};
};
}

