#include "wvegas_sender_bytes.h"
#include "unacked_packet_map.h"
#include "rtt_stats.h"
#include "logging.h"
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <string>
#include "couple_cc_manager.h"
namespace dqc{

namespace {
// Maximum window to allow when doing bandwidth resumption.
const QuicPacketCount kMaxResumptionCongestionWindow = 200;
// Constants based on TCP defaults.
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const float kRenoBeta = 0.7f;  // Reno backoff factor 0.7 in quic.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
const int initial_alpha = 2;
const int total_alpha = 10;
const int gamma = 1;
}  // namespace
#define MPTCP_WVEGAS_SCALE 16
static inline uint64_t mptcp_wvegas_scale(uint32_t val, int scale)
{
	return (uint64_t) val << scale;
}
static inline uint64_t mptcp_wvegas_rate(uint32_t cwnd, uint32_t rtt_us)
{
	return (mptcp_wvegas_scale(cwnd, MPTCP_WVEGAS_SCALE)/rtt_us);
}
WvegasSender::WvegasSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
    const UnackedPacketMap* unacked_packets,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    : rtt_stats_(rtt_stats),
    unacked_packets_(unacked_packets),
    stats_(stats),
    num_connections_(kDefaultNumConnections),
    min4_mode_(false),
    last_cutback_exited_slowstart_(false),
    num_acked_packets_(0),
    congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
    min_congestion_window_(kDefaultMinimumCongestionWindow),
    max_congestion_window_(max_congestion_window * kDefaultTCPMSS),
    slowstart_threshold_(max_congestion_window * kDefaultTCPMSS),
    initial_tcp_congestion_window_(initial_tcp_congestion_window *
                                   kDefaultTCPMSS),
    initial_max_tcp_congestion_window_(max_congestion_window *
                                       kDefaultTCPMSS),
    min_slow_start_exit_window_(min_congestion_window_),
    sample_rtt_(TimeDelta::Zero()),
    min_rtt_(TimeDelta::Infinite()),
    base_rtt_(TimeDelta::Infinite()),
    alpha_(initial_alpha),
    queue_delay_(TimeDelta::Zero()){
        weight_=mptcp_wvegas_scale(1, MPTCP_WVEGAS_SCALE);
    }

WvegasSender::~WvegasSender() {}
void WvegasSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}
void WvegasSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
    
    auto vrtt=rtt_stats_->latest_rtt()+TimeDelta::FromMicroseconds(1);
    sample_rtt_=sample_rtt_+vrtt;
    count_rtt_++;
    if(min_rtt_>vrtt){
        min_rtt_=vrtt;
    }
    if(base_rtt_>vrtt){
        base_rtt_=vrtt;
    }
    
    // lost is not handled in vegas, for recover
    for (const LostPacket& lost_packet : lost_packets) {
    OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                prior_in_flight);
    }
    for (const AckedPacket acked_packet : acked_packets) {
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                    prior_in_flight, event_time);
    }
}

void WvegasSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                        QuicByteCount acked_bytes,
                                        QuicByteCount prior_in_flight,
                                        ProtoTime event_time) {
  largest_acked_packet_number_.UpdateMax(acked_packet_number);
    if(largest_acked_packet_number_>beg_send_next_){
        beg_send_next_=largest_sent_packet_number_;
        if(count_rtt_<=2){
            MaybeIncreaseCwnd(acked_packet_number,acked_bytes,prior_in_flight,event_time);
        }else{
            num_acked_packets_=0;
            TimeDelta rtt=TimeDelta::FromMicroseconds(sample_rtt_.ToMicroseconds()/count_rtt_);
            uint64_t send_cwnd=congestion_window_/kDefaultTCPMSS;
            uint64_t target_cwnd=send_cwnd*base_rtt_.ToMicroseconds()/rtt.ToMicroseconds();
            CHECK(rtt>=base_rtt_);
            TimeDelta q_delay=rtt-base_rtt_;
            uint32_t diff=send_cwnd*q_delay.ToMicroseconds()/rtt.ToMicroseconds();
            if(diff>gamma&&InSlowStart()){
                congestion_window_=std::min(send_cwnd*kDefaultTCPMSS,(target_cwnd+1)*kDefaultTCPMSS);
                ExitSlowstart();
            }else if(InSlowStart()){
                congestion_window_ += kDefaultTCPMSS;
            }else{
                if(diff>=alpha_){
                    instant_rate_=mptcp_wvegas_rate(send_cwnd,rtt.ToMicroseconds());
                    CalculateWvegasWeight();
                    alpha_=std::max((uint32_t)2,(uint32_t)((weight_*total_alpha)>>MPTCP_WVEGAS_SCALE));
                }
                if(diff>alpha_){
                    congestion_window_ -= kDefaultTCPMSS;
                    ExitSlowstart();
                }else if(diff<alpha_){
                    congestion_window_ += kDefaultTCPMSS;
                }
                if(queue_delay_.IsZero()||queue_delay_>q_delay){
                    queue_delay_=q_delay;
                }
                if(q_delay>2*queue_delay_){
                    uint32_t backoff_factor=mptcp_wvegas_scale(base_rtt_.ToMicroseconds(),MPTCP_WVEGAS_SCALE)/(2*rtt.ToMicroseconds());
                    congestion_window_=send_cwnd*backoff_factor>>MPTCP_WVEGAS_SCALE;
                    queue_delay_=TimeDelta::Zero();
                }
                congestion_window_ = std::max(congestion_window_, min_congestion_window_);
                congestion_window_ = std::min(congestion_window_, max_congestion_window_);
            }
        }
        count_rtt_=0;
        sample_rtt_=TimeDelta::Zero();
        min_rtt_=TimeDelta::Infinite();
    }else if(InSlowStart()){
        congestion_window_ += kDefaultTCPMSS;
    }
}
void WvegasSender::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time) {
    if (!IsCwndLimited(prior_in_flight)) {
    return;
    }
    if (congestion_window_ >= max_congestion_window_) {
    return;
    }
    if (InSlowStart()) {
    // TCP slow start, exponential growth, increase by one for each ACK.
    congestion_window_ += kDefaultTCPMSS;
    /*QUIC_DVLOG(1)*/DLOG(INFO) << "Slow start; congestion window: " << congestion_window_
                    << " slowstart threshold: " << slowstart_threshold_;
    return;
    }
    ++num_acked_packets_;
    // Classic Reno congestion avoidance.
    // Divide by num_connections to smoothly increase the CWND at a faster rate
    // than conventional Reno.
    if (num_acked_packets_ * num_connections_ >=
        congestion_window_ / kDefaultTCPMSS) {
      congestion_window_ += kDefaultTCPMSS;
      num_acked_packets_ = 0;
    }
}
void WvegasSender::OnPacketSent(
    ProtoTime /*sent_time*/,
    QuicByteCount /*bytes_in_flight*/,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    HasRetransmittableData is_retransmittable) {
  if (InSlowStart()) {
    ++(stats_->slowstart_packets_sent);
  }

  if (is_retransmittable != HAS_RETRANSMITTABLE_DATA) {
    return;
  }
  if(!largest_sent_packet_number_.IsInitialized()){
      beg_send_next_=packet_number;
  }
  DCHECK(!largest_sent_packet_number_.IsInitialized() ||
         largest_sent_packet_number_ < packet_number);
  largest_sent_packet_number_ = packet_number;
}

bool WvegasSender::CanSend(QuicByteCount bytes_in_flight) {
  if (GetCongestionWindow() > bytes_in_flight) {
    return true;
  }
  if (min4_mode_ && bytes_in_flight < 4 * kDefaultTCPMSS) {
    return true;
  }
  return false;
}

QuicBandwidth WvegasSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (InRecovery() ? 1 : 1.25));
}

QuicBandwidth WvegasSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool WvegasSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool WvegasSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool WvegasSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool WvegasSender::ShouldSendProbingPacket() const {
  return false;
}

void WvegasSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  HandleRetransmissionTimeout();
}

std::string WvegasSender::GetDebugState() const {
  return "";
}

void WvegasSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void WvegasSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void WvegasSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void WvegasSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void WvegasSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void WvegasSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}
void WvegasSender::OnPacketLost(QuicPacketNumber packet_number,
                                QuicByteCount lost_bytes,
                                QuicByteCount prior_in_flight) {
  // TCP NewReno (RFC6582) says that once a loss occurs, any losses in packets
  // already sent should be treated as a single loss event, since it's expected.
  if (largest_sent_at_last_cutback_.IsInitialized() &&
      packet_number <= largest_sent_at_last_cutback_) {
    if (last_cutback_exited_slowstart_) {
      ++stats_->slowstart_packets_lost;
      stats_->slowstart_bytes_lost += lost_bytes;
    }
    return;
  }
  ++stats_->tcp_loss_events;
  last_cutback_exited_slowstart_ = InSlowStart();
  if (InSlowStart()) {
    ++stats_->slowstart_packets_lost;
  }
  
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  num_acked_packets_ = 0;
  slowstart_threshold_ = std::max(congestion_window_/2,min_congestion_window_);
}
QuicByteCount WvegasSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount WvegasSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}
void WvegasSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}
void WvegasSender::CalculateWvegasWeight(){
    uint64_t total_rate=0;
    total_rate+=instant_rate_;
    for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
        WvegasSender *sender=(*it);
        if(sender->CanSendNewPackets()&&(!SampleRtt().IsZero())){
            total_rate+=sender->InstantRate();
        }
    }
    if(total_rate>0){
        weight_=(mptcp_wvegas_scale(instant_rate_, MPTCP_WVEGAS_SCALE)/total_rate);
    }
}
void WvegasSender::OnConnectionMigration() {

  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType WvegasSender::GetCongestionControlType() const {
  return kWvegas;
}
void WvegasSender::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
	congestion_id_=cid;
	CoupleManager::Instance()->OnCongestionCreate(this);
}
void WvegasSender::RegisterCoupleCC(SendAlgorithmInterface*cc){
	bool exist=false;
	WvegasSender *sender=dynamic_cast<WvegasSender*>(cc);
    if(sender==this){return ;}
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		if(sender==(*it)){
			exist=true;
			break;
		}
	}
	if(!exist){
		other_ccs_.push_back(sender);
	}
}
void WvegasSender::UnRegisterCoupleCC(SendAlgorithmInterface*cc){
    WvegasSender *sender=dynamic_cast<WvegasSender*>(cc);
	if(!other_ccs_.empty()){
		other_ccs_.remove(sender);
	}
}
bool WvegasSender::CanSendNewPackets(){
    return CanSend(unacked_packets_->bytes_in_flight());
}
}
