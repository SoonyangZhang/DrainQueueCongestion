#include "proto_stream.h"
#include "proto_packets.h"
#include "flag_impl.h"
#include "proto_utils.h"
#include "logging.h"
namespace dqc{
const int max_send_buf_len=5*1024*1024;//5M
//when the occupied buffer is less than buffer_threshold,
//data can be injected into the buffer again, once a full buffer is reached.
const int buffer_threshold=2*max_send_buf_len/3;
ProtoStream::ProtoStream(ProtoConVisitor *visitor,uint32_t id)
:stream_id_(id)
,max_send_buf_len_(max_send_buf_len)
,water_send_buf_len_(max_send_buf_len_*2/3)
,visitor_(visitor)
,sequencer_(this)
,send_buf_(AbstractAlloc::Instance()){
}
bool ProtoStream::WriteDataToBuffer(std::string &piece){
    bool success=false;
    struct iovec io(MakeIovec(piece));
    uint32_t len=piece.size();
    if(mark_send_buf_full_){
        return success;
    }
    if((get_send_buffer_len()+len)>max_send_buf_len_){
        mark_send_buf_full_=true;
    }
	//bug
	//StreamOffset offset=send_buf_.BufferedOffset();
	//send_buf_.SaveMemData(&io,1,offset,len);
    send_buf_.SaveMemData(&io,1,0,len);
    //WriteBufferedData();
    return true;
}
void ProtoStream::OnAck(StreamOffset offset,ByteCount len){
    std::map<StreamOffset,ByteCount>::iterator found=sent_info_.find(offset);
    if(found!=sent_info_.end()){
        send_buf_.Acked(offset,len);
        sent_info_.erase(found);
        if(mark_send_buf_full_){
            if(get_send_buffer_len()<=water_send_buf_len_){
                mark_send_buf_full_=false;
            }
        }
    }else{
        DLOG(WARNING)<<offset<<" already acked";
    }
}
ByteCount ProtoStream::BufferedBytes() const{
    return send_buf_.BufferedOffset()-send_buf_.StreamBytesWritten();
}
ByteCount ProtoStream::Inflight() const{
    return send_buf_.Inflight();
}
void ProtoStream::set_max_send_buf_len(int max_send_buf_len){
    if(max_send_buf_len>0){
        max_send_buf_len_=max_send_buf_len;
        water_send_buf_len_=max_send_buf_len_*2/3;
    }
}
bool ProtoStream::HasBufferedData() const{
    return BufferedBytes()>0;
}
void ProtoStream::OnCanWrite(){
    if(!HasBufferedData()&&stream_visitor_){
        stream_visitor_->OnCanWrite();
    }
    WriteBufferedData();
}
bool ProtoStream::WriteStreamData(StreamOffset offset,ByteCount len,basic::DataWriter *writer){
    bool ret=false;
    ret=send_buf_.WriteStreamData(offset,len,writer);
    if(ret){
        std::map<StreamOffset,ByteCount>::iterator found=sent_info_.find(offset);
        if(found!=sent_info_.end()){
            DLOG(WARNING)<<"retransmission?";
        }else{
            sent_info_.insert(std::make_pair(offset,len));
        }
    }
    return ret;
}
void ProtoStream::OnStreamFrame(PacketStream &frame){
    sequencer_.OnFrameData(frame.offset,frame.data_buffer,frame.len);
}
void ProtoStream::OnDataAvailable(){
    //TODO
}
void ProtoStream::WriteBufferedData(){
    uint64_t payload_len=FLAG_packet_payload;
    uint64_t buffered=BufferedBytes();
    bool fin=false;
    if(buffered>0){
        uint64_t len=std::min(payload_len,buffered);
        StreamOffset offset=send_buf_.StreamBytesWritten();
        OnConsumedData(len);
        visitor_->WritevData(id(),offset,len,fin);
    }
}
void ProtoStream::OnConsumedData(ByteCount len){
    send_buf_.Consumed(len);
}
}//namespace dqc;
