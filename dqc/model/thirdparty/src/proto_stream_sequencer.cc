#include <algorithm>
#include "logging.h"
#include "proto_stream_sequencer.h"
namespace dqc{
//8*1024;
const uint16_t kBlockSize=1000;
ReadBuffer::ReadBuffer(StreamOffset offset,size_t block_size,Location loc){
    buf_=AbstractAlloc::Instance()->New(block_size,loc);
    cap_=block_size;
    offset_=offset;
}
ReadBuffer::~ReadBuffer(){
    if(buf_){
        AbstractAlloc::Instance()->Delete(buf_);
        buf_=nullptr;
    }
}
char * ReadBuffer::ReadWithoutCopy(size_t size){
    char *addr=buf_+r_pos_;
    //size_t off=std::min(size,ReadbleBytes());
    //r_pos_+=off;
    return addr;
}
int ReadBuffer::Write(const char*data,size_t size,size_t off){
    int ret =-1;
    size_t avail=cap_-off;
    size_t copy=std::min(avail,size);
    char *dst=buf_+off;
    if(copy>0&&w_info_.IsDisjoint(off,off+copy)){
        memcpy(dst,data,copy);
        w_info_.Add(off,off+copy);
        w_len_+=copy;
        ret=copy;
        ExtendReadPosR(off);
    }
    return ret;
}
size_t ReadBuffer::Comsumed(size_t size){
    size_t consumed=std::min(ReadbleBytes(),size);
    r_pos_+=consumed;
    return consumed;
}
void ReadBuffer::Reset(){
    w_info_.Clear();
    r_pos_=0;
    r_pos_r_=0;
    w_len_=0;
}
void ReadBuffer::ExtendReadPosR(size_t off){
    if(w_info_.Empty()){
        return;
    }
    if(r_pos_r_==off){
        auto it=w_info_.begin();
        size_t l=it->min();
        size_t r=it->max();
        if(0==l){
            r_pos_r_=r;
        }
    }
}
ProtoStreamSequencer::ProtoStreamSequencer(StreamInterface *stream,size_t max_buffer_capacity_bytes):
stream_(stream),
max_buffer_capacity_bytes_(max_buffer_capacity_bytes){

}
ProtoStreamSequencer::~ProtoStreamSequencer(){

}
void ProtoStreamSequencer::OnFrameData(StreamOffset o,const char *buf,size_t len){
    if(o<readable_offset_){
        DLOG(INFO)<<"duplicate "<<o;
        return ;
    }
    StreamOffset off_end=o+len;
    if(max_offset_<off_end){
        max_offset_=off_end;
        ExtendBlocks();
    }
    size_t w_index=GetBlockIndex(o);
    size_t w_offset=GetInBlockOffset(o);
    size_t remain=len;
    size_t write_len=0;
    ReadBuffer &readbuf=blocks_.at(w_index);
    size_t buf_off=0;
    write_len=readbuf.Write(buf+buf_off,remain,w_offset);
    remain-=write_len;
    buf_off+=write_len;
    while(remain>0){
        w_index+=1;
        ReadBuffer &readbuf=blocks_.at(w_index);
        write_len=readbuf.Write(buf+buf_off,remain,0);
        remain-=write_len;
        buf_off+=write_len;
    }
    CheckReadableSize();
}
size_t ProtoStreamSequencer::GetIovLength() const{
    return readble_len_/kBlockSize+2;
}
size_t ProtoStreamSequencer::GetReadableRegions(iovec* iov, size_t iov_len) const{
    size_t i=0;
    int read=0;
    int read_byte=ReadbleBytes();
    if(readble_len_>0){
    for(i=0;i<iov_len;i++){
        const ReadBuffer &const_buffer=blocks_.at(i);
        ReadBuffer *buffer=const_cast<ReadBuffer*>(&const_buffer);
        size_t block_read_size=buffer->ReadbleBytes();
        iov[i].iov_len=block_read_size;
        char *base=buffer->ReadWithoutCopy(block_read_size);
        iov[i].iov_base=(void*)base;
        read+=block_read_size;
        if(read>=read_byte){
            break;
        }
    }
    }
    return i+1;
}
//maybe have the choice do not consume all readble bytes;
void ProtoStreamSequencer::MarkConsumed(size_t num_bytes_consumed){
    auto vec_it=consuming_.begin();
    size_t remain=num_bytes_consumed;
    while(!blocks_.empty()){
        auto it=blocks_.begin();
        size_t conusmed=std::min(remain,*vec_it);
        conusmed=it->Comsumed(conusmed);
        remain-=conusmed;
        if(it->IsReadDone()){
            DLOG(INFO)<<"read done";
            blocks_.erase(it);
        }else{
            break;
        }
        if(0==remain){
            break;
        }
        vec_it++;
    }
}
size_t ProtoStreamSequencer::GetBlockIndex(StreamOffset o){
    StreamOffset start=blocks_.begin()->Offset();
    size_t i=(o-start)/kBlockSize;
    return i;
}
size_t ProtoStreamSequencer::GetInBlockOffset(StreamOffset o){
    return (o)%kBlockSize;
}
void ProtoStreamSequencer::ExtendBlocks(){
    if(blocks_r_off_>=max_offset_){
        return;
    }
    size_t need=(max_offset_-blocks_r_off_+kBlockSize-1)/kBlockSize;
    StreamOffset off=blocks_r_off_;
    for(size_t i=0;i<need;i++){
    ReadBuffer buf(off,kBlockSize,MY_FROM_HERE);
    off+=kBlockSize;
    blocks_.push_back(std::move(buf));
    }
    blocks_r_off_=off;
}
void ProtoStreamSequencer::CheckReadableSize(){
    readble_len_=0;
    std::vector<size_t> nullvec;
    consuming_.swap(nullvec);
    for(auto it=blocks_.begin();it!=blocks_.end();it++){
        size_t readable=it->ReadbleBytes();
        if(readable>0){
            consuming_.push_back(readable);
            readable_offset_+=readable;
            readble_len_+=readable;
        }
        if(!it->IsFull()){
            break;
        }
    }
    if(readble_len_>0&&stream_){
        stream_->OnDataAvailable();
    }
}
}
