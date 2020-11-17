#pragma once
#include <stdint.h>
#include <cstddef>
#include <vector>
#include <limits>
#ifdef WIN_32
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <errno.h>
#endif
//copy from razor project;
#include "cf_platform.h"
#include "packet_number.h"
#include "proto_time.h"
namespace dqc{
typedef uint64_t StreamOffset;
typedef StreamOffset QuicStreamOffset;
typedef uint64_t ByteCount;
typedef ByteCount QuicByteCount;
typedef uint64_t PacketCount;
typedef PacketCount QuicPacketCount;
typedef uint16_t PacketLength;
typedef PacketLength QuicPacketLength;
typedef QuicPacketNumber PacketNumber ;
typedef uint64_t TimeType;
typedef ProtoTime QuicTime;
struct AckedPacket;
// Information about a newly lost packet.
struct LostPacket {
  LostPacket(PacketNumber packet_number, PacketLength bytes_lost)
      : packet_number(packet_number), bytes_lost(bytes_lost) {}

  PacketNumber packet_number;
  // Number of bytes sent in the packet that was lost.
  PacketLength bytes_lost;
};
typedef std::vector<LostPacket> LostPacketVector;
typedef std::vector<AckedPacket> AckedPacketVector;
struct AckedPacket{
AckedPacket(PacketNumber seq,PacketLength len,ProtoTime ts)
:packet_number(seq),bytes_acked(len),receive_ts(ts){}
PacketNumber packet_number;
PacketLength bytes_acked;
ProtoTime receive_ts;
};
#if defined WIN_32
struct iovec{
    void *iov_base;
    size_t iov_len;
};
#else
#include <sys/uio.h>
#endif
enum ProtoPacketNumberLength:uint8_t{
    PACKET_NUMBER_1BYTE=1,
    PACKET_NUMBER_2BYTE=2,
    PACKET_NUMBER_3BYTE=3,
    PACKET_NUMBER_4BYTE=4,
    PACKET_NUMBER_6BYTE=6,
};
enum ProtoPacketNumberLengthFlag:uint8_t{
    PACKET_FLAG_1BYTE=0,
    PACKET_FLAG_2BYTE=1,
    PACKET_FLAG_3BYTE=1<<1,
    PACKET_FLAG_4BYTE=1<<1|1,
};
enum ProtoFrameType:uint8_t{
    PROTO_FRAME_STOP_WAITING=6,
    PROTO_PING_FRAME = 7,
    PROTO_FRAME_STREAM,
    PTOTO_FRAME_ACK,
};
//header is NULL;
struct ProtoPacketHeader{
    uint64_t con_id;
    PacketNumber packet_number;
    ProtoPacketNumberLength packet_number_length;
};
enum  HasRetransmittableData:uint8_t{
  NO_RETRANSMITTABLE_DATA,
  HAS_RETRANSMITTABLE_DATA,
};
// Used to return the result of processing a received ACK frame.
enum AckResult {
  PACKETS_NEWLY_ACKED,
  NO_PACKETS_NEWLY_ACKED,
  UNSENT_PACKETS_ACKED,     // Peer acks unsent packets.
  UNACKABLE_PACKETS_ACKED,  // Peer acks packets that are not expected to be
                            // acked. For example, encryption is reestablished,
                            // and all sent encrypted packets cannot be
                            // decrypted by the peer. Version gets negotiated,
                            // and all sent packets in the different version
                            // cannot be processed by the peer.
  PACKETS_ACKED_IN_WRONG_PACKET_NUMBER_SPACE,
};
//may be the stop waiting send along with stream frame?
//public header 0x00ll0000+seq,
// make these frame protocol similar to quic.
//https://blog.csdn.net/tq08g2z/article/details/77311763

// Defines for all types of congestion control algorithms that can be used in
// QUIC. Note that this is separate from the congestion feedback type -
// some congestion control algorithms may use the same feedback type
// (Reno and Cubic are the classic example for that).
enum CongestionControlType { 
kRenoBytes,kRenoPlus,
kCubicBytes,kC2TcpBytes,kCubicPlus,
kElastic,
kVeno,kWestwood,kMpWest,
kBalia,kLiaBytes,kLiaEnhance,kLiaEnhance2,kLiaEnhance3,
kNmccBytes,kOlia,kWvegas,kMpVeno,
kDwcBytes,kCoupleBBR,kBBR_DELAY, 
kBBR,kBBRD,kBBRPlus,
kBBRRand,kTsunami,kHighSpeedRail,
kGoogCC,kBBRv2,kBBRv2Ecn,
kCopa,kPCC,kVivace,
kWebRTCVivace,kVegas,
kLedbat,kLpTcp,kLpBBR,kLpBBRNo,
kLearningBytes,kLearningBytesHalf,
kHunnanBytes,kXmpBytes,
kDctcp,
kQuicBBR,kQuicBBRD};
ProtoPacketNumberLength ReadPacketNumberLength(uint8_t flag);
ProtoPacketNumberLengthFlag PktNumLen2Flag(ProtoPacketNumberLength byte);
ProtoPacketNumberLength GetMinPktNumLen(PacketNumber seq);
}//namespace dqc;
