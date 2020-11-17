#pragma once
#include <string>
#include <deque>
#include <vector>
#include "interval.h"
#include "proto_types.h"
#include "proto_utils.h"
namespace dqc{
class ReadBuffer{
public:
    ReadBuffer(StreamOffset offset,size_t block_size,Location loc);
    ~ReadBuffer();
    ReadBuffer(ReadBuffer &&r){
        *this=std::move(r);
    }
    size_t length(){
        return cap_;
    }
    ReadBuffer& operator=(ReadBuffer &&r){
        w_info_.Swap(&(r.w_info_));
        r_pos_=r.r_pos_;
        r_pos_r_=r.r_pos_r_;
        w_len_=r.w_len_;
        cap_=r.cap_;
        offset_=r.offset_;
        buf_=r.buf_;
        r.buf_=nullptr;
        r.cap_=0;
        r.Reset();
        return *this;
    }
    size_t ReadbleBytes() const{
        return r_pos_r_-r_pos_;
    }
    size_t WritebleSize() const{
        return cap_-w_len_;
    }
    char *ReadWithoutCopy(size_t size);
    int Write(const char*data,size_t size,size_t off);
    size_t Comsumed(size_t size);
    bool IsFull() const{
        return w_len_==cap_;
    }
    bool IsReadDone() const{
        return r_pos_==cap_;
    }
    StreamOffset Offset() const{
        return offset_;
    }
private:
    void Reset();
    void ExtendReadPosR(size_t off);
    IntervalSet<size_t> w_info_;
    size_t r_pos_{0};
    size_t r_pos_r_{0};
    size_t w_len_{0};
    size_t cap_{0};
    StreamOffset offset_{0};
    char *buf_{nullptr};
};
class ProtoStreamSequencer{
public:
class StreamInterface{
public:
    virtual void OnDataAvailable()=0;
    virtual ~StreamInterface(){}
};
public:
    ProtoStreamSequencer(StreamInterface *stream,size_t max_buffer_capacity_bytes=1024);
    ~ProtoStreamSequencer();
    void OnFrameData(StreamOffset o,const char *buf,size_t len);
    size_t ReadbleBytes() const{ return readble_len_ ;}
    size_t GetIovLength() const;
    size_t GetReadableRegions(iovec* iov, size_t iov_len) const;
    void MarkConsumed(size_t num_bytes_consumed);
private:
    size_t GetBlockIndex(StreamOffset o);
    size_t GetInBlockOffset(StreamOffset o);
    void ExtendBlocks();
    void CheckReadableSize();
    //IntervalSet<StreamOffset>  bytes_received_;
    //std::set<StreamOffset> waiting_offset_;
    StreamOffset readable_offset_{0};
    StreamOffset max_offset_{0};
    size_t readble_len_{0};
    std::deque<ReadBuffer> blocks_;
    StreamOffset blocks_l_off_{0};
    StreamOffset blocks_r_off_{0};
    StreamInterface *stream_{nullptr};
    // The maximum total capacity of this buffer in byte, as constructed.
    const size_t max_buffer_capacity_bytes_;
    std::vector<size_t> consuming_;
};
}
