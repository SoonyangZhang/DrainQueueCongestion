#pragma once
#include <map>
#include <memory>
#include <utility>
#include <deque>
#include "proto_types.h"
#include "proto_utils.h"
#include "byte_codec.h"
#include "interval.h"
namespace dqc{
class MyAlloc:public AbstractAlloc{
public:
    MyAlloc(){}
    ~MyAlloc(){}
    virtual char *New(size_t size,Location loc) override;
    virtual char *New(size_t size) override;
    virtual void Delete(char *buf) override;
    bool IsAllReturn();
    virtual void PrintAllocInfo() override;
    virtual void CheckMemLeak() override;
    class LocationSize{
    public:
        LocationSize(Location loc,size_t size){
            loc_=loc;
            size_=size;
        }
        LocationSize(const LocationSize &o){
            *this=(o);
        }
        LocationSize &operator=(const LocationSize &o){
            this->loc_=o.loc_;
            this->size_=o.size_;
            return *this;
        }
        ~LocationSize(){}
        Location loc_;
        size_t size_;
    };
private:
    #if !defined (NDEBUG)
    typedef std::map<char*,LocationSize>::iterator iterator;
    std::map<char*,LocationSize> allocated_;
    #endif
};
class Buffer{
public:
    Buffer(AbstractAlloc *alloc,int len,Location loc);
    ~Buffer();
    Buffer(Buffer&&o){
        *this=std::move(o);
    }
    Buffer& operator=(Buffer&&o){
        data_=o.data_;
        o.data_=nullptr;
        return *this;
    }
    char *data() const{
        return data_;
    }
private:
    AbstractAlloc *alloc_{nullptr};
    char *data_{nullptr};
};
class MemSlice{
public:
    MemSlice(AbstractAlloc *alloc,int len);
    MemSlice(AbstractAlloc *alloc,int len,Location loc);
    MemSlice(MemSlice &&o){
        (*this)=std::move(o);
    }
    MemSlice& operator=(MemSlice &&o);
    ~MemSlice(){}
    char *data() const{
        if(buf_.get()){
            return buf_.get()->data();
        }
        return nullptr;
    }
    int length(){
        return len_;
    }
    void Reset(){
        buf_=nullptr;
    }
    bool Empty(){
        return buf_==nullptr;
    }
private:
    std::unique_ptr<Buffer> buf_;
    int len_;
};
class BufferSlice{
public:
   BufferSlice(MemSlice slice,StreamOffset off)
   :slice_(std::move(slice)),off_(off){}
    BufferSlice(BufferSlice&& o)=default;
    BufferSlice& operator=(BufferSlice&& o)=default;
   ~BufferSlice(){}
    MemSlice slice_;
    StreamOffset off_;
};
class StreamSendBuffer{
public:
    typedef std::deque<BufferSlice>::iterator DequeItor;
    StreamSendBuffer(AbstractAlloc*alloc);
    ~StreamSendBuffer();
    void SaveMemData(const struct iovec *iov,int iov_count,
                     size_t iov_offset,ByteCount len);
    bool WriteStreamData(StreamOffset offset,ByteCount len,basic::DataWriter *writer);
    void Consumed(ByteCount len);
    bool Acked(StreamOffset offset,ByteCount len);
    ByteCount Inflight() const{
        return bytes_inflight_;
    }
    uint64_t StreamBytesWritten() const{
        return stream_bytes_written_;
    }
    uint64_t BufferedOffset() const{
        return offset_;
    }
private:
    void SaveSlice(MemSlice slice,ByteCount len);
    bool FreeSlices(StreamOffset start,StreamOffset stop);
    void CleanUpBufferedSlice();
    std::deque<BufferSlice> buf_slices_;
    IntervalSet<StreamOffset> bytes_acked_;
    StreamOffset offset_{0};
    StreamOffset stream_bytes_written_{0};
    AbstractAlloc *alloc_{nullptr};
    int32_t write_index_{-1};
    // inflight Bytes, waiting to be acked
    uint64_t bytes_inflight_{0};
};
}//namespace dqc;
