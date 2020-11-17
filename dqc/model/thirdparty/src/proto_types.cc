#include "proto_types.h"
#include <stdint.h>
namespace dqc{
ProtoPacketNumberLength ReadPacketNumberLength(uint8_t flag){
    switch(flag&PACKET_FLAG_4BYTE){
    case PACKET_FLAG_1BYTE:{
        return PACKET_NUMBER_1BYTE;
    }
    case PACKET_FLAG_2BYTE:{
        return PACKET_NUMBER_2BYTE;
    }
    case PACKET_FLAG_3BYTE:{
        return PACKET_NUMBER_3BYTE;
    }
    case PACKET_FLAG_4BYTE:{
        return PACKET_NUMBER_4BYTE;
    }
    }
}
ProtoPacketNumberLengthFlag PktNumLen2Flag(ProtoPacketNumberLength byte){
    switch(byte){
        case PACKET_NUMBER_1BYTE:{
            return PACKET_FLAG_1BYTE;
        }
        case PACKET_NUMBER_2BYTE:{
            return PACKET_FLAG_2BYTE;
        }
        case PACKET_NUMBER_3BYTE:{
            return PACKET_FLAG_3BYTE;
        }
        case PACKET_NUMBER_4BYTE:{
            return PACKET_FLAG_4BYTE;
        }
    }
    return PACKET_FLAG_4BYTE;
}
#include <stdio.h>
ProtoPacketNumberLength GetMinPktNumLen(PacketNumber seq){
    if(seq.ToUint64()<(UINT32_C(1)<<(PACKET_NUMBER_1BYTE*8))){
        return PACKET_NUMBER_1BYTE;
    }else if(seq.ToUint64()<(UINT32_C(1)<<(PACKET_NUMBER_2BYTE*8))){
        return PACKET_NUMBER_2BYTE;
    }else if(seq.ToUint64()<(UINT32_C(1)<<(PACKET_NUMBER_3BYTE*8))){
        return PACKET_NUMBER_3BYTE;
    }else if(seq.ToUint64()<(UINT64_C(1)<<(PACKET_NUMBER_4BYTE*8))){
        return PACKET_NUMBER_4BYTE;
    }
        return PACKET_NUMBER_4BYTE;
}
}//namespace dqc;
