#include "proto_utils.h"
#include <algorithm>
#include <memory.h>
#include <stdio.h>
namespace dqc{
std::string Location::ToString() const{
    char buf[256];
    snprintf(buf,sizeof(buf),"%s@%s",fun_name_,file_line_);
    return buf;
}
struct iovec MakeIovec(std::string&data){
    struct iovec iov=
    {const_cast<char*>(data.data()),data.size()};
    return iov;
}
void CopyToBuffer(const struct iovec*iov,int iov_count,
                  size_t iov_offset,size_t buf_len,char*buf){
    int i=0;
    size_t data_len=0;
    for(i=0;i<iov_count;i++){
        if((data_len+iov[i].iov_len)>iov_offset){
            break;
        }
    data_len+=iov[i].iov_len;
    }
    size_t start=iov_offset-data_len;
    char *dst=buf;
    const size_t iov_available=iov[i].iov_len-start;
    size_t copy_len=std::min(iov_available,buf_len);
    char *src=static_cast<char*>(iov[i].iov_base)+start;
    //memcpy(dst,src,copy_len);
    //buf_len-=copy_len;
    //i++;
    while(true){

        memcpy(dst,src,copy_len);
        buf_len-=copy_len;
        dst+=copy_len;
        if(buf_len==0||(++i)>=iov_count){
            break;
        }
        src=static_cast<char*>(iov[i].iov_base);
        copy_len=std::min(iov[i].iov_len,buf_len);
    }
}
ObjectCreator* ObjectCreator::Instance(){
    static ObjectCreator *ins=new ObjectCreator(AbstractAlloc::Instance());
    return ins;
}
RandomLetter* RandomLetter::Instance(){
    static RandomLetter *ins=new RandomLetter();
    return ins;
}
RandomLetter::RandomLetter(){
    random_.seed(123456);
}
char RandomLetter::GetLetter(){
    char a='a';
    char z='z';
    int min=0;
    int max=z-a;
    int index=random_.nextInt(min,max);
    char x=a+index;
    return x;
}
}//namespace dqc;
