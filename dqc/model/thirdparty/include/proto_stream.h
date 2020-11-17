#pragma once
#include "memslice.h"
#include "proto_con_visitor.h"
#include "proto_stream_data_producer.h"
#include "proto_stream_sequencer.h"
#include <string>
namespace dqc{
struct PacketStream;
class ProtoStream:public ProtoStreamSequencer::StreamInterface{
public:
    class StreamCanWriteVisitor{
    public:
        virtual ~StreamCanWriteVisitor(){}
        virtual void OnCanWrite()=0;
    };
    ProtoStream(ProtoConVisitor *visitor,uint32_t id);
    ~ProtoStream(){}
    void set_stream_vistor(StreamCanWriteVisitor *visitor){ stream_visitor_=visitor;}
    bool WriteDataToBuffer(std::string &piece);
    void OnAck(StreamOffset offset,ByteCount len);
    ByteCount get_send_buffer_len () const{
        return BufferedBytes()+Inflight();
    }
    ByteCount BufferedBytes() const;
    ByteCount Inflight() const;
    void set_max_send_buf_len(int max_send_buf_len=0);
    bool HasBufferedData() const;
    void OnCanWrite();
    bool WriteStreamData(StreamOffset offset,ByteCount len,basic::DataWriter *writer);
    uint32_t id(){
        return stream_id_;
    }
    virtual void OnStreamFrame(PacketStream &frame);
    virtual void OnDataAvailable() override;
private:
    void WriteBufferedData();
    void OnConsumedData(ByteCount len);
    uint32_t stream_id_{0};
    ByteCount max_send_buf_len_;
    ByteCount water_send_buf_len_;
    bool mark_send_buf_full_{false};
    ProtoConVisitor *visitor_{nullptr};
    ProtoStreamSequencer sequencer_;
    StreamSendBuffer send_buf_;
    std::map<StreamOffset,ByteCount> sent_info_;
    StreamCanWriteVisitor *stream_visitor_{nullptr};
};
}
