#include "olia_sender_bytes.h"
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
const float kRenoBeta = 0.5f;  // Reno backoff factor 0.7 in quic.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.
const QuicByteCount kDefaultMinimumCongestionWindow = 2 * kDefaultTCPMSS;
const int scale = 10;
}  // namespace
static inline uint64_t mptcp_olia_scale(uint64_t val, int scale)
{
	return (uint64_t) val << scale;
}
OliaSender::OliaSender(
    const ProtoClock* clock,
    const RttStats* rtt_stats,
    QuicPacketCount initial_tcp_congestion_window,
    QuicPacketCount max_congestion_window,
    QuicConnectionStats* stats)
    : rtt_stats_(rtt_stats),
      stats_(stats),
      num_connections_(kDefaultNumConnections),
      min4_mode_(false),
      last_cutback_exited_slowstart_(false),
      slow_start_large_reduction_(false),
      no_prr_(false),
      num_acked_packets_(0),
      congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
      min_congestion_window_(kDefaultMinimumCongestionWindow),
      max_congestion_window_(max_congestion_window * kDefaultTCPMSS),
      slowstart_threshold_(max_congestion_window * kDefaultTCPMSS),
      initial_tcp_congestion_window_(initial_tcp_congestion_window *
                                     kDefaultTCPMSS),
      initial_max_tcp_congestion_window_(max_congestion_window *
                                         kDefaultTCPMSS),
      min_slow_start_exit_window_(min_congestion_window_) {}

OliaSender::~OliaSender() {}
void OliaSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}

float OliaSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}

void OliaSender::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    ProtoTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
  if (rtt_updated && InSlowStart() &&
      hybrid_slow_start_.ShouldExitSlowStart(
          rtt_stats_->latest_rtt(), rtt_stats_->min_rtt(),
          GetCongestionWindow() / kDefaultTCPMSS)) {
    ExitSlowstart();
  }
  for (const LostPacket& lost_packet : lost_packets) {
    OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                 prior_in_flight);
  }
  for (const AckedPacket acked_packet : acked_packets) {
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                  prior_in_flight, event_time);
  }
}

void OliaSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                        QuicByteCount acked_bytes,
                                        QuicByteCount prior_in_flight,
                                        ProtoTime event_time) {
  largest_acked_packet_number_.UpdateMax(acked_packet_number);
  //reference from mpquic
  ca_.mptcp_loss3+=acked_bytes;
  if (InRecovery()) {
    if (!no_prr_) {
      // PRR is used when in recovery.
      prr_.OnPacketAcked(acked_bytes);
    }
    return;
  }
  MaybeIncreaseCwnd(acked_packet_number, acked_bytes, prior_in_flight,
                    event_time);
  if (InSlowStart()) {
    hybrid_slow_start_.OnPacketAcked(acked_packet_number);
  }
}

void OliaSender::OnPacketSent(
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
  if (InRecovery()) {
    // PRR is used when in recovery.
    prr_.OnPacketSent(bytes);
  }
  DCHECK(!largest_sent_packet_number_.IsInitialized() ||
         largest_sent_packet_number_ < packet_number);
  largest_sent_packet_number_ = packet_number;
  hybrid_slow_start_.OnPacketSent(packet_number);
}

bool OliaSender::CanSend(QuicByteCount bytes_in_flight) {
  if (!no_prr_ && InRecovery()) {
    // PRR is used when in recovery.
    return prr_.CanSend(GetCongestionWindow(), bytes_in_flight,
                        GetSlowStartThreshold());
  }
  if (GetCongestionWindow() > bytes_in_flight) {
    return true;
  }
  if (min4_mode_ && bytes_in_flight < 4 * kDefaultTCPMSS) {
    return true;
  }
  return false;
}

QuicBandwidth OliaSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (no_prr_ && InRecovery() ? 1 : 1.25));
}

QuicBandwidth OliaSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool OliaSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool OliaSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool OliaSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool OliaSender::ShouldSendProbingPacket() const {
  return false;
}

void OliaSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

std::string OliaSender::GetDebugState() const {
  return "";
}

void OliaSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void OliaSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void OliaSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void OliaSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void OliaSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void OliaSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void OliaSender::OnPacketLost(QuicPacketNumber packet_number,
                                       QuicByteCount lost_bytes,
                                       QuicByteCount prior_in_flight) {
  // TCP NewReno (RFC6582) says that once a loss occurs, any losses in packets
  // already sent should be treated as a single loss event, since it's expected.
  if (largest_sent_at_last_cutback_.IsInitialized() &&
      packet_number <= largest_sent_at_last_cutback_) {
    if (last_cutback_exited_slowstart_) {
      ++stats_->slowstart_packets_lost;
      stats_->slowstart_bytes_lost += lost_bytes;
      if (slow_start_large_reduction_) {
        // Reduce congestion window by lost_bytes for every loss.
        congestion_window_ = std::max(congestion_window_ - lost_bytes,
                                      min_slow_start_exit_window_);
        slowstart_threshold_ = congestion_window_;
      }
    }
    /*QUIC_DVLOG(1)*/DLOG(INFO)<< "Ignoring loss for largest_missing:" << packet_number
                  << " because it was sent prior to the last CWND cutback.";
    return;
  }
  ++stats_->tcp_loss_events;
  last_cutback_exited_slowstart_ = InSlowStart();
  if (InSlowStart()) {
    ++stats_->slowstart_packets_lost;
  }

  if (!no_prr_) {
    prr_.OnPacketLost(prior_in_flight);
  }
  // TODO(b/77268641): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else{
    congestion_window_ = congestion_window_ *RenoBeta();
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  // Reset packet count from congestion avoidance mode. We start counting again
  // when we're out of recovery.
  num_acked_packets_ = 0;
  struct mptcp_olia *ca=get_ca();
  if(ca->mptcp_loss3 != ca->mptcp_loss2){
      ca->mptcp_loss1 = ca->mptcp_loss2;
      ca->mptcp_loss2 = ca->mptcp_loss3;
  }
  DLOG(INFO)<< "Incoming loss; congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_;
}

QuicByteCount OliaSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount OliaSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void OliaSender::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time) {
  //QUIC_BUG_IF(InRecovery()) << "Never increase the CWND during recovery.";
  // Do not increase the congestion window unless the sender is close to using
  // the current window.
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
  if(!other_ccs_.empty()){
	  bool subflows_exit_slow_start=true;
	  for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		  OliaSender *sender=(*it);
		  if(sender->InSlowStart()){
			  subflows_exit_slow_start=false;
			  break;
		  }
	  }
	  if(subflows_exit_slow_start){
          uint64_t inc_num, inc_den, rate, cwnd_scaled;
          mptcp_olia *ca=get_ca();
          mptcp_get_epsilon();
          rate=mptcp_get_rate();
          uint64_t send_cwnd=congestion_window_/kDefaultTCPMSS;
          cwnd_scaled = mptcp_olia_scale(send_cwnd, scale);
          inc_den = ca->epsilon_den *send_cwnd * rate ? : 1;
          
          
          if (ca->epsilon_num == -1) {
              if (ca->epsilon_den * cwnd_scaled * cwnd_scaled < rate) {
                  inc_num = rate - ca->epsilon_den *
                      cwnd_scaled * cwnd_scaled;
                  ca->mptcp_snd_cwnd_cnt -=(
                      mptcp_olia_scale(inc_num , scale)/inc_den);
              } else {
                  inc_num = ca->epsilon_den *
                      cwnd_scaled * cwnd_scaled - rate;
                  ca->mptcp_snd_cwnd_cnt +=(
                      mptcp_olia_scale(inc_num , scale)/inc_den);
              }
          } else {
              inc_num = ca->epsilon_num * rate +
                  ca->epsilon_den * cwnd_scaled * cwnd_scaled;
              ca->mptcp_snd_cwnd_cnt +=(
                  mptcp_olia_scale(inc_num , scale)/inc_den);
          }
          
          
          if (ca->mptcp_snd_cwnd_cnt >= (1 << scale) - 1) {
              if (congestion_window_ < max_congestion_window_)
                  congestion_window_+=kDefaultTCPMSS;
              ca->mptcp_snd_cwnd_cnt = 0;
          } else if (ca->mptcp_snd_cwnd_cnt <= 0 - (1 << scale) + 1) {
               congestion_window_=std::max(congestion_window_,kDefaultTCPMSS);
               congestion_window_= std::max(min_congestion_window_, congestion_window_-kDefaultTCPMSS);
               ca->mptcp_snd_cwnd_cnt = 0;
          }
	  }else{
		    if (num_acked_packets_ * num_connections_ >=
		        congestion_window_ / kDefaultTCPMSS) {
		      congestion_window_ += kDefaultTCPMSS;
		      num_acked_packets_ = 0;
		    }
	  }
  }else{
	    if (num_acked_packets_ * num_connections_ >=
	        congestion_window_ / kDefaultTCPMSS) {
	      congestion_window_ += kDefaultTCPMSS;
	      num_acked_packets_ = 0;
	    }
  }
  DLOG(INFO)<< "Lia congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_
                << " congestion window count: " << num_acked_packets_;

}

void OliaSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void OliaSender::OnConnectionMigration() {
  hybrid_slow_start_.Restart();
  prr_ = PrrSender();
  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType OliaSender::GetCongestionControlType() const {
  return kOlia;
}
void OliaSender::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
	congestion_id_=cid;
	CoupleManager::Instance()->OnCongestionCreate(this);
}
void OliaSender::RegisterCoupleCC(SendAlgorithmInterface*cc){
	bool exist=false;
    OliaSender *sender=dynamic_cast<OliaSender*>(cc);
    if(this==sender) {return ;}
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
void OliaSender::UnRegisterCoupleCC(SendAlgorithmInterface*cc){
    OliaSender *sender=dynamic_cast<OliaSender*>(cc);
	if(!other_ccs_.empty()){
		other_ccs_.remove(sender);
	}
}
OliaSender::mptcp_olia* OliaSender::get_ca(){
    return &ca_;
}
uint64_t OliaSender::get_srtt_us(){
	return rtt_stats_->smoothed_rtt().ToMicroseconds();
}
uint32_t OliaSender::mptcp_get_crt_cwnd(){
    uint32_t send_cwnd=GetCongestionWindow()/kDefaultTCPMSS;
    if(InRecovery()){
        send_cwnd=slowstart_threshold_/kDefaultTCPMSS;
    }
    return send_cwnd;
}
uint64_t OliaSender::mptcp_get_rate(){
    uint32_t path_rtt=get_srtt_us();
    uint64_t rate=1;
    uint32_t tmp_cwnd=mptcp_get_crt_cwnd();
    rate+=mptcp_olia_scale(tmp_cwnd, scale);
    for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
        OliaSender *sender=(*it);
        uint64_t scaled_num;
        tmp_cwnd=sender->mptcp_get_crt_cwnd();
        scaled_num=mptcp_olia_scale(tmp_cwnd, scale) * path_rtt;
        rate +=(scaled_num/sender->get_srtt_us());
    }
    rate*=rate;
    return rate;
}
void OliaSender::mptcp_get_epsilon(){
    std::list<OliaSender*> ccs;
    ccs.push_back(this);
    for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
        OliaSender *sender=(*it);
        ccs.push_back(sender);
    }
	uint64_t tmp_int, tmp_rtt, best_int = 0, best_rtt = 1;
	uint32_t max_cwnd=0, tmp_cwnd, established_cnt = 0;
	uint8_t M = 0, B_not_M = 0;
    //mptcp_get_max_cwnd
    for(auto it=ccs.begin();it!=ccs.end();it++){
        OliaSender *sender=(*it);
        tmp_cwnd = sender->mptcp_get_crt_cwnd();
        if(tmp_cwnd>max_cwnd){
            max_cwnd=tmp_cwnd;
        }
    }
    struct mptcp_olia *ca=nullptr;
    for(auto it=ccs.begin();it!=ccs.end();it++){
        OliaSender *sender=(*it);
        ca=sender->get_ca();
        established_cnt++;
        tmp_rtt=sender->get_srtt_us();
        tmp_rtt*=tmp_rtt;
		tmp_int = std::max(ca->mptcp_loss3 - ca->mptcp_loss2,
			      ca->mptcp_loss2 - ca->mptcp_loss1);
        if((uint64_t)tmp_int * best_rtt>=(uint64_t)best_int * tmp_rtt){
            best_rtt = tmp_rtt;
            best_int = tmp_int;
        }
    }
    for(auto it=ccs.begin();it!=ccs.end();it++){
        OliaSender *sender=(*it);
        ca=sender->get_ca();
        tmp_cwnd = sender->mptcp_get_crt_cwnd();
        if(tmp_cwnd==max_cwnd){
            M++;
        }else{
            tmp_rtt=sender->get_srtt_us();
            tmp_rtt*=tmp_rtt;
            tmp_int = std::max(ca->mptcp_loss3 - ca->mptcp_loss2,
            ca->mptcp_loss2 - ca->mptcp_loss1);
            if((uint64_t)tmp_int * best_rtt == (uint64_t)best_int * tmp_rtt){
                B_not_M++;
            }
        }
    }
    for(auto it=ccs.begin();it!=ccs.end();it++){
        OliaSender *sender=(*it);
        ca=sender->get_ca();
        if(B_not_M == 0){
            ca->epsilon_num = 0;
            ca->epsilon_den = 1;
        }else{
            tmp_rtt=sender->get_srtt_us();
            tmp_rtt*=tmp_rtt;
            tmp_int = std::max(ca->mptcp_loss3 - ca->mptcp_loss2,
            ca->mptcp_loss2 - ca->mptcp_loss1);
            tmp_cwnd = sender->mptcp_get_crt_cwnd();
            if(tmp_cwnd < max_cwnd&&
            (uint64_t)tmp_int * best_rtt == (uint64_t)best_int * tmp_rtt){
                ca->epsilon_num = 1;
                ca->epsilon_den = established_cnt * B_not_M;
            }else if(tmp_cwnd == max_cwnd){
                ca->epsilon_num = -1;
                ca->epsilon_den = established_cnt * M;
            }else{
                ca->epsilon_num = 0;
                ca->epsilon_den = 1;
            }
        }
    }
}
}
