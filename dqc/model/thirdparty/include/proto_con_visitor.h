#pragma once
#include "proto_types.h"
namespace dqc{
class ProtoConVisitor{
public:
    virtual void WritevData(uint32_t id,StreamOffset offset,ByteCount len,bool fin)=0;
    virtual ~ProtoConVisitor(){}
};
}//namespace dqc;
