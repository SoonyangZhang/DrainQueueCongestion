#pragma once
#include <string>
#include "proto_types.h"
#include "random.h"
#define STRINGIZE_NO_EXPANSION(x) #x
//black magic
#define MY_PROTO_STRINGNIZE(x) STRINGIZE_NO_EXPANSION(x)
#define MY_FROM_HERE MY_FROM_HERE_FUNC(__FUNCTION__)
#define MY_FROM_HERE_FUNC(fun) \
Location(fun,__FILE__ ":" MY_PROTO_STRINGNIZE(__LINE__))
namespace dqc{
class Location{
public:
    Location(const char *fun_name,const char *file_line):
    fun_name_(fun_name),file_line_(file_line){

    }
    Location():fun_name_("Unknow"),file_line_("Unknow"){}
    Location(const Location &o):fun_name_(o.fun_name_)
    ,file_line_(o.file_line_){
    }
    Location& operator=(const Location &o){
        fun_name_=o.fun_name_;
        file_line_=o.file_line_;
        return *this;
    }
    ~Location(){}
    std::string ToString() const;
private:
    const char * fun_name_;
    const char * file_line_;
};
class  AbstractAlloc{
public:
    static AbstractAlloc* Instance();
    virtual char *New(size_t size,Location loc)=0;
    virtual char *New(size_t size)=0;
    virtual void Delete(char *buf)=0;
    virtual void PrintAllocInfo()=0;
    virtual void CheckMemLeak()=0;
    virtual ~AbstractAlloc(){}
};
class ObjectCreator{
public:
    static ObjectCreator* Instance();
    template<class T,typename...U>
    T* New(Location loc,U&&...u){
        T *obj;
        char *buf=alloc_->New(sizeof(T),loc);
        obj=new (buf) T(std::forward<U>(u)...);
        return obj;
    }
    template<class T>
    void Delete(T*obj){
        obj->~T();
        alloc_->Delete((char*)obj);
    }
private:
    ObjectCreator(AbstractAlloc *alloc){
        alloc_=alloc;
    }
    ~ObjectCreator(){}
    AbstractAlloc *alloc_{nullptr};
};
class RandomLetter{
public:
    static RandomLetter*Instance();
    char GetLetter();
private:
    RandomLetter();
    ~RandomLetter();
    Random random_;
};
struct iovec MakeIovec(std::string&data);
void CopyToBuffer(const struct iovec*iov,int iov_count,
                  size_t iov_offset,size_t buf_len,char*buf);
}//namespace dqc;
