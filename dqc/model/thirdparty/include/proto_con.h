#pragma once
#include "proto_comm.h"
#include "proto_con_visitor.h"
#include "proto_stream.h"
#include "proto_packets.h"
#include "send_packet_manager.h"
#include "proto_framer.h"
#include "proto_stream_data_producer.h"
#include "proto_socket.h"
#include "alarm.h"
#include "proto_connection_stats.h"
#include <deque>
#include <map>
#include <stdint.h>
namespace dqc{
class ProtoCon:public ProtoConVisitor,StreamAckedObserver,
ProtoFrameVisitor,ProtoStreamDataProducer{
public:
class TraceSentSeq{
public:
    virtual ~TraceSentSeq(){}
    virtual void OnSent(PacketNumber seq,ProtoTime sent_ts){};
};
public:
    ProtoCon(ProtoClock *clock,AlarmFactory *alarm_factory,CongestionControlType cc);
    ~ProtoCon();
    QuicBandwidth EstimatedBandwidth() const{
    	return sent_manager_.BandwidthEstimate();
    }
	SendPacketManager * GetSentPacketManager() {return &sent_manager_;}
    void ProcessUdpPacket(SocketAddress &self,SocketAddress &peer,
                          const ProtoReceivedPacket& packet);
    ProtoStream *GetOrCreateStream(uint32_t id);
    void Close(uint32_t id);
    void set_packet_writer(Socket *writer){
        packet_writer_=writer;
    }
    void SetTraceSentSeq(TraceSentSeq *cb){trace_sent_ =cb;}
    void SetTraceLossPacketDelay(TraceLossPacketDelay cb){sent_manager_.SetTraceLossPacketDelay(std::move(cb));}
    void SetMaxBandwidth(uint32_t bps);
    void set_peer(SocketAddress &peer){peer_=peer;}
    void Process();
    void SendInitData();
    void SetThisCongestionId(uint32_t cid);
    void SetThisNumEmulatedConnections(int num_connections);    

    virtual void WritevData(uint32_t id,StreamOffset offset,ByteCount len,bool fin) override;
    virtual void OnAckStream(uint32_t id,StreamOffset off,ByteCount len) override;
    //framevisitor
    virtual bool OnStreamFrame(PacketStream &frame) override;
    virtual void OnError(ProtoFramer* framer) override;
    virtual void OnEcnMarkCount(uint64_t ecn_ce_count) override;
    virtual bool OnAckFrameStart(PacketNumber largest_acked,
                                 TimeDelta ack_delay_time) override;
    virtual bool OnAckRange(PacketNumber start, PacketNumber end) override;
    virtual bool OnAckTimestamp(PacketNumber packet_number,
                                ProtoTime timestamp) override;
    virtual bool OnAckFrameEnd(PacketNumber start) override;
    //ProtoStreamDataProducer
    virtual bool WriteStreamData(uint32_t id,
                                 StreamOffset offset,
                                 ByteCount len,
                                 basic::DataWriter *writer) override;
    void OnCanWrite();
    void OnFastRetransmit();
    void OnRetransmissionTimeOut();
    const TimeDelta GetRetransmissionDelay() const{
        return sent_manager_.GetRetransmissionDelay();
    }
    PacketNumber GetMaxSentSeq() const { return seq_;}
    uint32_t GetFastRetrans() const {return fast_retrans_;}
	std::pair<PacketNumber,TimeDelta> GetOneWayDelayInfo(){return sent_manager_.GetOneWayDelayInfo();}
private:
    PacketNumber AllocSeq(){
        return seq_++;
    }
    void MaybeSendBulkData();
    void OnCanWriteSession();
    void WriteNewData();
    bool CanWrite(HasRetransmittableData has_retrans);
    ProtoStream *CreateStream();
    ProtoStream *GetStream(uint32_t id);
    void NotifyCanSendToStreams();
    int Send();
    bool SendRetransPending(TransType tt);
    void Retransmit(uint32_t id,StreamOffset off,ByteCount len,bool fin,TransType tt);
    void SendStopWaitingFrame();
    void UpdateReleaseTimeIntoFuture();
    ProtoClock *clock_{nullptr};
    std::map<uint32_t,ProtoStream*> streams_;
    std::deque<PacketStream> waiting_info_;
    uint32_t stream_id_{0};
    PacketNumber seq_{1};
    ProtoTime time_of_last_received_packet_;
    ProtoTime new_ack_received_time_{ProtoTime::Zero()};
    SendPacketManager sent_manager_;
    SocketAddress peer_;
    bool first_packet_from_peer_{true};
    ProtoFramer frame_encoder_;
    ProtoFramer frame_decoder_;
    Socket *packet_writer_{nullptr};
    AlarmFactory *alarm_factory_{nullptr};
    std::shared_ptr<Alarm> send_alarm_;
	 std::shared_ptr<Alarm> fast_retrans_alarm_;
    TraceSentSeq *trace_sent_{nullptr};
	QuicConnectionStats con_stats_;
    bool supports_release_time_{false};
    TimeDelta release_time_into_future_{TimeDelta::Zero()};
    uint32_t fast_retrans_{0};
    uint32_t cid_{0};
};
}//namespace dqc;
