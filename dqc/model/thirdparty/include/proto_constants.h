#pragma once
#include <cstdint>
#include <limits>
#include "proto_types.h"
namespace dqc{
// Simple time constants.
const uint64_t kNumSecondsPerMinute = 60;
const uint64_t kNumSecondsPerHour = kNumSecondsPerMinute * 60;
const uint64_t kNumSecondsPerWeek = kNumSecondsPerHour * 24 * 7;
const uint64_t kNumMillisPerSecond = 1000;
const uint64_t kNumMicrosPerMilli = 1000;
const uint64_t kNumMicrosPerSecond = kNumMicrosPerMilli * kNumMillisPerSecond;

// Do not allow initial congestion window to be greater than 200 packets.
const QuicPacketCount kMaxInitialCongestionWindow = 200;

// Do not allow initial congestion window to be smaller than 10 packets.
const QuicPacketCount kMinInitialCongestionWindow = 10;

// Maximum number of tracked packets.
const QuicPacketCount kMaxTrackedPackets = 10000;

// Default number of connections for N-connection emulation.
const uint32_t kDefaultNumConnections = 2;
// Default initial maximum size in bytes of a QUIC packet.
const QuicByteCount kDefaultMaxPacketSize = 1350;
// Default initial maximum size in bytes of a QUIC packet for servers.
const QuicByteCount kDefaultServerMaxPacketSize = 1000;
// Maximum transmission unit on Ethernet.
const QuicByteCount kEthernetMTU = 1500;
// The maximum packet size of any QUIC packet over IPv6, based on ethernet's max
// size, minus the IP and UDP headers. IPv6 has a 40 byte header, UDP adds an
// additional 8 bytes.  This is a total overhead of 48 bytes.  Ethernet's
// max packet size is 1500 bytes,  1500 - 48 = 1452.
const QuicByteCount kMaxV6PacketSize = 1452;
// The maximum packet size of any QUIC packet over IPv4.
// 1500(Ethernet) - 20(IPv4 header) - 8(UDP header) = 1472.
const QuicByteCount kMaxV4PacketSize = 1472;
// The maximum incoming packet size allowed.
const QuicByteCount kMaxIncomingPacketSize = kMaxV4PacketSize;
// The maximum outgoing packet size allowed.
const QuicByteCount kMaxOutgoingPacketSize = kMaxV6PacketSize;
// ETH_MAX_MTU - MAX(sizeof(iphdr), sizeof(ip6_hdr)) - sizeof(udphdr).
const QuicByteCount kMaxGsoPacketSize = 65535 - 40 - 8;
// Default maximum packet size used in the Linux TCP implementation.
// Used in QUIC for congestion window computations in bytes.
const QuicByteCount kDefaultTCPMSS = 1460;
const QuicByteCount kMaxSegmentSize = kDefaultTCPMSS;


// TCP RFC calls for 1 second RTO however Linux differs from this default and
// define the minimum RTO to 200ms, we will use the same until we have data to
// support a higher or lower value.
static const int64_t kMinRetransmissionTimeMs = 200;
}
