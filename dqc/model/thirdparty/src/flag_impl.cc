#include "flag_impl.h"
#define PROTO_FLAG(type,flag,value) type flag=value;
#include "flag_list.h"
#undef PROTO_FLAG
