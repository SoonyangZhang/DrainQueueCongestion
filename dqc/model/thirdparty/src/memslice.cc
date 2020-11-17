#include "memslice.h"
#include "flag_impl.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
namespace dqc{
char *MyAlloc::New(size_t size,Location loc){
    char *addr=new char [size];
#if !defined (NDEBUG)
    LocationSize loz=LocationSize(loc,size);
    allocated_.insert(std::make_pair(addr,loz));
#endif // defined
    return addr;
}
char *MyAlloc::New(size_t size){
    Location loc;
    return New(size,loc);
}
void MyAlloc::Delete(char *buf){
#if !defined (NDEBUG)
    iterator found=allocated_.find(buf);
    if(found!=allocated_.end()){
        allocated_.erase(found);
    }else{
        DLOG(FATAL)<<"addr is not allcated";
    }
    #endif // defined
    delete [] buf;
}
bool MyAlloc::IsAllReturn(){
    bool ret=true;
#if !defined (NDEBUG)
    if(!allocated_.empty()){
        ret=false;
    }
#endif // defined
    return ret;
}
void MyAlloc::PrintAllocInfo(){
#if !defined (NDEBUG)
iterator itor;
for(itor=allocated_.begin();itor!=allocated_.end();itor++){
   char *addr=itor->first;
   int len=itor->second.size_;
   printf("addr %p, %d\n",addr,len);
}
#endif // defined
}
void MyAlloc::CheckMemLeak(){
#if !defined (NDEBUG)
iterator itor;
for(itor=allocated_.begin();itor!=allocated_.end();itor++){
   int len=itor->second.size_;
   printf("not free %s %d\n",itor->second.loc_.ToString().c_str(),len);
}
#endif // defined
}
AbstractAlloc *AbstractAlloc::Instance(){
    static AbstractAlloc *ins=new MyAlloc();
    return ins;
}
Buffer::Buffer(AbstractAlloc *alloc,int len,Location loc){
    alloc_=alloc;
    if(alloc_){
        data_=alloc_->New(len,loc);
    }else{
        data_=new char [len];
    }
}
Buffer::~Buffer(){
    if(alloc_){
        if(data_){
            alloc_->Delete(data_);
        }
    }else{
        if(data_){
            delete data_;
        }
    }
}
MemSlice::MemSlice(AbstractAlloc *alloc,int len){
    buf_.reset(new Buffer(alloc,len,MY_FROM_HERE));
    len_=len;
}
MemSlice::MemSlice(AbstractAlloc *alloc,int len,Location loc){
    buf_.reset(new Buffer(alloc,len,loc));
    len_=len;
}
MemSlice&MemSlice::operator=(MemSlice &&o){
    buf_=std::move(o.buf_);
    len_=o.len_;
    return *this;
}
StreamSendBuffer::StreamSendBuffer(AbstractAlloc*alloc)
:alloc_(alloc){

}
StreamSendBuffer::~StreamSendBuffer(){
    alloc_=nullptr;
    offset_=0;
}
void StreamSendBuffer::SaveMemData(const struct iovec *iov,int iov_count,
                     size_t iov_offset,ByteCount len){
    const ByteCount max_slice_len=GetFlagImpl(FLAG_max_send_buf_size);
    while(len>0){
        size_t slice_len=std::min(len,max_slice_len);
        MemSlice slice(alloc_,slice_len,MY_FROM_HERE);
        CopyToBuffer(iov,iov_count,iov_offset,slice_len,const_cast<char*>(slice.data()));
        SaveSlice(std::move(slice),slice_len);
        len-=slice_len;
        iov_offset+=slice_len;
    }
}

void StreamSendBuffer::SaveSlice(MemSlice slice,ByteCount len){
    buf_slices_.emplace_back(BufferSlice(std::move(slice),offset_));
    offset_+=len;

}
bool StreamSendBuffer::FreeSlices(StreamOffset start,StreamOffset stop){
    auto it=buf_slices_.begin();
    if(it==buf_slices_.end()||it->slice_.Empty()){
        return false;
    }
    for(;it!=buf_slices_.end();it++){
        if(start>=it->off_&&start<(it->off_+it->slice_.length())){
            break;
        }
    }
    if(it==buf_slices_.end()||it->slice_.Empty()){
        return false;
    }
    for(;it!=buf_slices_.end();it++){
        if(!it->slice_.Empty()&&
           bytes_acked_.Contain(it->off_,it->off_+it->slice_.length())){
            DLOG(WARNING)<<"free"<<it->off_<<" "<<it->off_+it->slice_.length();
            it->slice_.Reset();
        }
        if(it->off_>=stop){
            break;
        }
    }
    return true;
}
void StreamSendBuffer::CleanUpBufferedSlice(){
    while(!buf_slices_.empty()&&buf_slices_.front().slice_.Empty()){
        buf_slices_.pop_front();
    }

}
bool StreamSendBuffer::WriteStreamData(StreamOffset offset,ByteCount len,basic::DataWriter *writer){
    //bool ret=false;
    //bool write_index_hit = false;
    DequeItor itor=buf_slices_.begin();
    /*if(write_index_!=-1){
        if(offset>=(itor->off_+itor->slice_.length())){
            return false;
        }
    }*/

    /*if (offset>=itor->off_) {
      write_index_hit = true;
    } else {
      // Write index missed, move iterator to the beginning.
      itor = buf_slices_.begin();
    }*/
    int copy_len=0;
    char *src;
    int available_len=0;
    for(;itor!=buf_slices_.end();itor++){
        if(len==0||offset<itor->off_){
            break;
        }
        if(offset>=(itor->off_+itor->slice_.length())){
            continue;
        }
        available_len=itor->slice_.length();
        StreamOffset slice_offset=offset-itor->off_;
        src=const_cast<char*>(itor->slice_.data())+
        (slice_offset);
        available_len-=slice_offset;
        int total=len;
        copy_len=std::min(available_len,total);
        writer->WriteBytes((void*)src,copy_len);
        len-=copy_len;
        offset+=copy_len;
    }
    return len==0;
}
void StreamSendBuffer::Consumed(ByteCount len){
    bytes_inflight_+=len;
    stream_bytes_written_+=len;
}
bool StreamSendBuffer::Acked(StreamOffset offset,ByteCount len){
    ByteCount acked=std::min(bytes_inflight_,len);
    bytes_inflight_-=acked;
    bytes_acked_.Add(offset,offset+len);
    if(!FreeSlices(offset,offset+len)){
        return false;
    }
    CleanUpBufferedSlice();
    return true;
}
}//namespace dqc;
