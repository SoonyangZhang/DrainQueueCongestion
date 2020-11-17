#include "proto_send_algorithm_interface.h"

#include "balia_sender_bytes.h"
//#include "proto_bbr2_sender.h"
#include "quic_bbr2_sender.h"
#include "quic_bbr_sender.h"
#include "couple_bbr_sender.h"
#include "cubic_plus_sender_bytes.h"
#include "dwc_sender_bytes.h"
#include "lia_sender_bytes.h"
#include "lia_sender_enhance.h"
#include "lia_sender_enhance3.h"
#include "nmcc_sender_bytes.h"
#include "olia_sender_bytes.h"
#include "pcc_sender.h"

#include "proto_bbr_plus_sender.h"
#include "proto_bbr_sender.h"
#include "proto_bbr_rand_sender.h"
#include "proto_copa_sender.h"
#include "proto_dctcp_sender.h"
#include "proto_delay_bbr_sender.h"
#include "proto_highrail_sender.h"
#include "proto_tsunami_sender.h"
#include "rtt_stats.h"
#include "tcp_c2tcp_sender_bytes.h"
#include "tcp_cubic_sender_bytes.h"
#include "tcp_elastic_sender_bytes.h"
#include "tcp_hunnan_sender_bytes.h"
#include "tcp_learning_sender_bytes.h"
#include "tcp_veno_sender_bytes.h"
#include "tcp_westwood_sender_bytes.h"
#include "vegas_sender_bytes.h"
#include "wvegas_sender_bytes.h"
#include "ledbat_sender_bytes.h"
#include "proto_lpbbr_sender.h"
#include "mp_veno_sender_bytes.h"
#include "mp_westwood_sender_bytes.h"
#include "lptcp_sender_bytes.h"
#include "xmp_sender_bytes.h"
namespace dqc{
SendAlgorithmInterface * SendAlgorithmInterface::Create(
        const ProtoClock *clock,
        const RttStats *rtt_stats,
        const UnackedPacketMap* unacked_packets,
        CongestionControlType type,
        Random *random,
		QuicConnectionStats* stats,
        QuicPacketCount initial_congestion_window){
    QuicPacketCount max_congestion_window = kDefaultMaxCongestionWindowPackets;
    switch(type){
        case kRenoBytes:{
            return new TcpCubicSenderBytes(clock,
                               rtt_stats,
                               true,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kCubicBytes:{
            return new TcpCubicSenderBytes(clock,
                               rtt_stats,
                       		   false,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kC2TcpBytes:{
            return new TcpC2tcpSenderBytes(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kElastic:{
            return new TcpElasticSenderBytes(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kVeno:{
            return new TcpVenoSenderBytes(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kWestwood:{
            return new TcpWestwoodSenderBytes(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kMpWest:{
            return new MpWestwoodSenderBytes(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kCubicPlus:{
            return new CubicPlusSender(clock,
                               rtt_stats,
							   unacked_packets,
                       		   false,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,random
                               );
        }
        case kRenoPlus:{
            return new CubicPlusSender(clock,
                               rtt_stats,
							   unacked_packets,
                       		   true,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,random
                               );
        }
        case kBalia:{
            return new BaliaSender(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kLiaBytes:{
            return new LiaSender(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kLiaEnhance:{
            return new LiaSenderEnhance(clock,
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,false
                               );
        }
        case kLiaEnhance2:{
            return new LiaSenderEnhance(clock,
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,true
                               );
        }
        case kLiaEnhance3:{
            return new LiaSenderEnhance3(clock,
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kNmccBytes:{
            return new NmccSender(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kOlia:{
            return new OliaSender(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kMpVeno:{
            return new MpVenoSender(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kWvegas:{
            return new WvegasSender(clock,
                               rtt_stats,
							   unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kDwcBytes:{
            return new DwcSender(clock,
                               rtt_stats,
                       		   true,
                               initial_congestion_window,
                               max_congestion_window,
                               stats
                               );
        }
        case kCoupleBBR:{
            return new CoupleBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kBBR_DELAY:{
            return new DelayBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kBBR:{
            return new ProtoBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,false
                               );
        }
        case kBBRD:{
            return new ProtoBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,true
                               );
        }
        case kBBRPlus:{
            return new BbrPlusSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kBBRRand:{
            return new BbrRandSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kTsunami:{
            return new TsunamiSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kHighSpeedRail:{
            return new HighSeedRailSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kBBRv2:{
            return new Bbr2Sender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,
				stats);
        }
        case kBBRv2Ecn:{
            return new Bbr2Sender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,
				stats,true);
        }
        case kCopa:{
            return new CopaSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kPCC:{
            return new PccSender(rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random
                               );
        }
        case kVivace:{
            return new PccSender(rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,kVivaceUtility
                               );
        }
        case kWebRTCVivace:{
            return new PccSender(rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,kModifyVivaceUtility
                               );
        }
        case kVegas:{
            return new VegasSender(clock,
                               rtt_stats,
							   unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kLedbat:{
            return new LedbatSender(clock,
                               rtt_stats,
							   unacked_packets,1,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kLpTcp:{
            return new LpTcpSender(clock,
                               rtt_stats,
							   unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kLpBBR:{
            return new LpBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,true
                               );
        }
        case kLpBBRNo:{
            return new LpBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,false
                               );
        }
        case kLearningBytes:{
            return new TcpLearningSenderBytes(clock,
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,random,false);
        }
        case kLearningBytesHalf:{
            return new TcpLearningSenderBytes(clock,
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,random,true);
        }
        case kHunnanBytes:{
            return new TcpHunnanSenderBytes(clock,
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats,random);
        }
        case kXmpBytes:{
            return new XmpSenderBytes(clock,
                               rtt_stats,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kDctcp:{
            return new ProtoDctcpSender(clock,
                               rtt_stats,unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               stats);
        }
        case kQuicBBR:{
            return new QuicBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,stats
                               );
        }
        case kQuicBBRD:{
            return new QuicBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,stats,true
                               );
        }
        default:{
            return new ProtoBbrSender(clock->Now(),
                               rtt_stats,
                               unacked_packets,
                               initial_congestion_window,
                               max_congestion_window,
                               random,false
                               );
        }
    }
}
}
