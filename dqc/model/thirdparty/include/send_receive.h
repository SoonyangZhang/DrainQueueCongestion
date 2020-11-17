#pragma once
#include "proto_con.h"
#include "received_packet_manager.h"
#include "proto_socket.h"
#include "interval.h"
#include "process_alarm_factory.h"
namespace dqc{
/*
class FakeAckFrameReceive{
public:
    virtual ~FakeAckFrameReceive(){}
    virtual void OnPeerData(SocketAddress &peer,char *data,int len,ProtoTime &recipt_time)=0;
};
class SimulateSender:public FakeAckFrameReceive{
public:
    SimulateSender();
    ~SimulateSender();
    void set_address(SocketAddress addr){local_=addr;}
    void set_packet_writer(Socket *sock);
    void Process();
    void DataGenerator(int times);
    void OnPeerData(SocketAddress &peer,char *data,
                    int len,ProtoTime &recipt_time) override;
private:
    Socket *sock_{nullptr};
    ProtoCon connection_;
    ProtoStream *stream_{nullptr};
    SocketAddress local_;
    uint32_t stream_id_{0};
};
class SimulateReceiver :public ProtoFrameVisitor, public Socket{
public:
    SimulateReceiver();
    ~SimulateReceiver();
    void set_address(SocketAddress addr){local_=addr;}
    void set_ack_sink(FakeAckFrameReceive *sink){feed_ack_=sink;}
    bool OnStreamFrame(PacketStream &frame) override;
    void OnError(ProtoFramer* framer) override;
    bool OnAckFrameStart(PacketNumber largest_acked,
                         TimeDelta ack_delay_time) override;
    bool OnAckRange(PacketNumber start,
                    PacketNumber end) override;
    bool OnAckTimestamp(PacketNumber packet_number,
                        ProtoTime timestamp) override;
    bool OnAckFrameEnd(PacketNumber start) override;
    bool OnStopWaitingFrame(const PacketNumber least_unacked) override;
    int SendTo(const char*buf,size_t size,SocketAddress &dst) override;
    void SendAckFrame(ProtoTime now);
    PacketNumber AllocSeq(){
        return seq_++;
    }
private:
    Socket *sock_{nullptr};
    ReceivdPacketManager received_packet_manager_;
    ProtoFramer frame_decoder_;
    ProtoFramer framer_encoder_;
    FakeAckFrameReceive *feed_ack_{nullptr};
    SocketAddress local_;
    ProtoTime base_time_{ProtoTime::Zero()};
    TimeDelta offset_{TimeDelta::FromMilliseconds(10)};
    TimeDelta one_way_delay_{TimeDelta::FromMilliseconds(100)};
    int count_{1};
    PacketNumber seq_{1};
};*/
class Sender:public ProtoStream::StreamCanWriteVisitor{
public:
    Sender(ProtoClock *clock);
    ~Sender();
    void Bind(const char *ip,uint16_t port);
    SocketAddress &get_local_addr(){ return local_;};
    void set_peer(SocketAddress &peer);
    void set_test_rto_flag(bool flag){
        test_rto_flag_=flag;
    }
    void Process();
    void DataGenerator(int times);
    void OnCanWrite() override{
        DataGenerator(2);
    }
private:
    ProtoClock *clock_{nullptr};
    ProtoTime rto_{ProtoTime::Zero()};
    MainEngine time_driver_;
    std::shared_ptr<AlarmFactory> alarm_factory_;
    ProtoCon connection_;
    ProtoStream *stream_{nullptr};
    Socket *fd_{nullptr};
    SocketAddress local_;
    bool test_rto_flag_{false};
    bool running_{true};
    //SocketAddress peer_;
    uint32_t stream_id_{0};
    int data_generated_index_{0};
    int total_generated_{2000};
};
class Receiver:public ProtoFrameVisitor{
public:
    Receiver(ProtoClock *clock);
    ~Receiver();
    void Bind(const char *ip,uint16_t port);
    SocketAddress &get_local_addr(){ return local_;};
    void Process();
    void set_nerver_feedack(bool feed){
        nerver_feed_ack_=feed;
    }
    bool OnStreamFrame(PacketStream &frame) override;
    void OnError(ProtoFramer* framer) override;
    bool OnAckFrameStart(PacketNumber largest_acked,
                         TimeDelta ack_delay_time) override;
    bool OnAckRange(PacketNumber start,
                    PacketNumber end) override;
    bool OnAckTimestamp(PacketNumber packet_number,
                        ProtoTime timestamp) override;
    bool OnAckFrameEnd(PacketNumber start) override;
    bool OnStopWaitingFrame(const PacketNumber least_unacked) override;
private:
    void SendAckFrame(ProtoTime now);
    PacketNumber AllocSeq(){
        return seq_++;
    }
    ProtoClock *clock_{nullptr};
    ReceivdPacketManager received_packet_manager_;
    ProtoFramer frame_decoder_;
    ProtoFramer framer_encoder_;
    Socket *fd_{nullptr};
    SocketAddress local_;
    SocketAddress peer_;
    PacketNumber seq_{1};
    bool nerver_feed_ack_{false};
    IntervalSet<StreamOffset> recv_interval_;
};
//void simu_send_receiver_test();
}
