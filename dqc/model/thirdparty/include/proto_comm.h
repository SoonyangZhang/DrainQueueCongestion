#pragma once
#include "proto_types.h"
#include "basic_constants.h"
namespace dqc{
enum SentPacketState:uint8_t{
    SPS_OUT,
    SPS_ACKED,
    SPS_NERVER_SENT,
    SPS_UN_ACKABLE,
    SPS_LOST,
    SPS_RETRANSED,
};
enum TransType:uint8_t{
    TT_NO_RETRANS,
    TT_FIRST_TRANS=TT_NO_RETRANS,
    TT_LOSS_RETRANS,
	TT_FAST_RETRANS,
    TT_RTO_RETRANS,
};
class StreamAckedObserver{
public:
   virtual void OnAckStream(uint32_t id,StreamOffset off,ByteCount len)=0;
   virtual ~StreamAckedObserver(){}
};
}//namespace dqc;
