#include "string_utils.h"
#include <stdarg.h>
namespace dqc{
//https://blog.csdn.net/yockie/article/details/52796842
//int snprintf(char *str, size_t size, const char *format, ...);
//int vsnprintf(char *str, size_t size, const char *format, va_list ap);
std::string ProtoStringPrintf(const char *format,...){
    size_t i=0;
    char buf[512]={'\0'};
    va_list ap;
    va_start(ap, format);
    i=vsnprintf(buf,512,format,ap);
    va_end(ap);
    return std::string(buf,i);
}
}
