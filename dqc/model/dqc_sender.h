#pragma once
#include <memory>
#include <vector>
#include "ns3/event-id.h"
#include "ns3/callback.h"
#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/dqc_clock.h"
#include "ns3/proto_stream.h"
#include "ns3/proto_bandwidth.h"
#include "ns3/proto_time.h"
#include "ns3/packet_number.h"
#include "ns3/socket_address.h"
#include "ns3/proto_socket.h"
#include "ns3/process_alarm_factory.h"
#include "ns3/proto_con.h"
namespace ns3{
class DqcSender;
class OneWayDelaySink{
public:
    virtual ~OneWayDelaySink(){}
    virtual bool NeedRegisterToSender(uint32_t id){ return false;}
    virtual void OnOneWayDelaySample(uint32_t id,uint32_t seq,uint32_t owd){}
};
class FakePackeWriter:public dqc::Socket{
public:
	FakePackeWriter(DqcSender *sender):m_sender(sender){}
	~FakePackeWriter(){}
	int SendTo(const char*buf,size_t size,dqc::SocketAddress &dst) override;
private:
	DqcSender *m_sender{nullptr};
};
class DqcSender: public Application,
public dqc::ProtoStream::StreamCanWriteVisitor,
public dqc::ProtoCon::TraceSentSeq{
public:
    DqcSender(bool ecn=false);
    DqcSender(dqc::CongestionControlType cc_type,bool ecn=false,bool engine_time=true); 
    ~DqcSender(){}
    void SetMaxBandwidth(uint32_t bps);
    typedef Callback<void,int32_t> TraceBandwidth;
    void SetBwTraceFuc(TraceBandwidth cb);
    typedef Callback<void,int32_t> TraceSentSeq;
    void SetSentSeqTraceFuc(TraceSentSeq cb);
    typedef Callback<void,uint32_t,uint32_t> TraceLossPacketDelay;
    void SetTraceLossPacketDelay(TraceLossPacketDelay cb);
    typedef Callback<void,uint32_t,uint32_t> TraceOwdAtSender;
    void SetTraceOwdAtSender(TraceOwdAtSender cb);
    void Bind(uint16_t port);
    InetSocketAddress GetLocalAddress();
    void ConfigurePeer(Ipv4Address addr,uint16_t port);    
    void OnCanWrite() override{
        DataGenerator(2);
    }
    void SendToNetwork(Ptr<Packet> p);
    void OnSent(dqc::PacketNumber seq,dqc::ProtoTime sent_ts) override;
    void OnPacketLossInfo(dqc::PacketNumber seq,uint32_t rtt);
    uint32_t GetId() const {return m_id;}
    void SetSenderId(uint32_t id);
    void RegisterOnewayDelaySink(OneWayDelaySink *sink);
    void SetCongestionId(uint32_t cid);
	void SetNumEmulatedConnections(int num_connections);
private:
	void DataGenerator(int times);
	virtual void StartApplication() override;
	virtual void StopApplication() override;
    void RecvPacket(Ptr<Socket> socket);
    void Process();
    void CheckNoPacketOut();
    void EngineEvent();
    void UpdateEngineEvent();
    void PostProceeAfterReceiveFromPeer();
    bool m_ecn{false};
    bool m_running{false};
    uint32_t m_id{0};
    FakePackeWriter m_writer;
    Ipv4Address m_peerIp;
    uint16_t m_peerPort;
    uint16_t m_bindPort;
    dqc::SocketAddress m_self;
    dqc::SocketAddress m_remote;
    uint32_t m_streamId{0};
    Ptr<Socket> m_socket;
    DqcSimuClock m_clock;
    dqc::MainEngine m_timeDriver;
    std::shared_ptr<dqc::AlarmFactory> m_alarmFactory;
    dqc::ProtoCon m_connection;
    dqc::ProtoStream *m_stream{nullptr};
    bool m_enableEngineTimer{false};
    EventId m_engineTimer;
    EventId m_processTimer;
    int64_t m_packetInteval{100};//0.5 ms
    int m_packetGenerated{0};
	bool m_pakcetLimit{false};
    int m_packetAllowed{50000};
    std::vector<OneWayDelaySink*> m_sinks;
	TraceBandwidth m_traceBwCb;
	int64_t m_lastSentTs{0};
    dqc::PacketNumber m_lastAckedSeq{dqc::PacketNumber(0)};
	TraceSentSeq m_traceSentSeqCb;
    TraceLossPacketDelay m_traceLossDelay;
    TraceOwdAtSender m_traceOwd;
};   
}
