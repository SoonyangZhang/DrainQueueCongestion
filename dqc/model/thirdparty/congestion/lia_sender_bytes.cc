#include "lia_sender_bytes.h"
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
static const int alpha_scale_den = 10;
static const int alpha_scale_num = 32;
static const int alpha_scale = 12;
}  // namespace
static inline uint64_t mptcp_ccc_scale(uint32_t val, int scale)
{
	return (uint64_t) val << scale;
}
LiaSender::LiaSender(
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

LiaSender::~LiaSender() {}
void LiaSender::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}

float LiaSender::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}

void LiaSender::OnCongestionEvent(
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

void LiaSender::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                        QuicByteCount acked_bytes,
                                        QuicByteCount prior_in_flight,
                                        ProtoTime event_time) {
  largest_acked_packet_number_.UpdateMax(acked_packet_number);
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

void LiaSender::OnPacketSent(
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

bool LiaSender::CanSend(QuicByteCount bytes_in_flight) {
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

QuicBandwidth LiaSender::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (no_prr_ && InRecovery() ? 1 : 1.25));
}

QuicBandwidth LiaSender::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool LiaSender::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool LiaSender::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool LiaSender::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool LiaSender::ShouldSendProbingPacket() const {
  return false;
}

void LiaSender::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

std::string LiaSender::GetDebugState() const {
  return "";
}

void LiaSender::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void LiaSender::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void LiaSender::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void LiaSender::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void LiaSender::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

void LiaSender::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void LiaSender::OnPacketLost(QuicPacketNumber packet_number,
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
  mptcp_ccc_recalc_alpha();
  // TODO(b/77268641): Separate out all of slow start into a separate class.
  if (slow_start_large_reduction_ && InSlowStart()) {
    DCHECK_LT(kDefaultTCPMSS, congestion_window_);
    if (congestion_window_ >= 2 * initial_tcp_congestion_window_) {
      min_slow_start_exit_window_ = congestion_window_ / 2;
    }
    congestion_window_ = congestion_window_ - kDefaultTCPMSS;
  } else{
    congestion_window_ = congestion_window_ * RenoBeta();
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
  // Reset packet count from congestion avoidance mode. We start counting again
  // when we're out of recovery.
  num_acked_packets_ = 0;
  /*QUIC_DVLOG(1)*/DLOG(INFO)<< "Incoming loss; congestion window: " << congestion_window_
                << " slowstart threshold: " << slowstart_threshold_;
}

QuicByteCount LiaSender::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount LiaSender::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void LiaSender::MaybeIncreaseCwnd(
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
		  LiaSender *sender=(*it);
		  if(sender->InSlowStart()){
			  subflows_exit_slow_start=false;
			  break;
		  }
	  }
	  if(subflows_exit_slow_start){
		  mptcp_ccc_recalc_alpha();
		  uint64_t send_cwnd=0;
		  send_cwnd =mptcp_ccc_scale(1, alpha_scale)/alpha_;
		  if(send_cwnd<congestion_window_ / kDefaultTCPMSS){
			  send_cwnd=congestion_window_ / kDefaultTCPMSS;
		  }
		  if(num_acked_packets_*num_connections_>=send_cwnd){
			  congestion_window_ += kDefaultTCPMSS;
			  num_acked_packets_=0;
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

void LiaSender::HandleRetransmissionTimeout() {
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void LiaSender::OnConnectionMigration() {
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

CongestionControlType LiaSender::GetCongestionControlType() const {
  return kLiaBytes;
}
void LiaSender::SetCongestionId(uint32_t cid){
	if(congestion_id_!=0||cid==0){
		return;
	}
	congestion_id_=cid;
	CoupleManager::Instance()->OnCongestionCreate(this);
}
void LiaSender::RegisterCoupleCC(SendAlgorithmInterface*cc){
	bool exist=false;
    LiaSender *sender=dynamic_cast<LiaSender*>(cc);
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
void LiaSender::UnRegisterCoupleCC(SendAlgorithmInterface*cc){
    LiaSender *sender=dynamic_cast<LiaSender*>(cc);
	if(!other_ccs_.empty()){
		other_ccs_.remove(sender);
	}
}
uint64_t LiaSender::get_srtt_us() const{
	return rtt_stats_->smoothed_rtt().ToMicroseconds();
}
void LiaSender::mptcp_ccc_recalc_alpha(){
	uint64_t max_numerator=0,sum_denominator = 0, alpha = 1;
	uint32_t best_cwnd = 0;
	uint64_t best_rtt = 0;
	uint32_t send_cwnd=GetCongestionWindow()/kDefaultTCPMSS;
	uint64_t srtt=get_srtt_us();
	best_rtt=srtt;
	best_cwnd=send_cwnd;
	max_numerator=mptcp_ccc_scale(send_cwnd,
			alpha_scale_num)/(srtt*srtt);
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		LiaSender *sender=(*it);
		send_cwnd=sender->GetCongestionWindow()/kDefaultTCPMSS;
		srtt=sender->get_srtt_us();
		uint64_t tmp=mptcp_ccc_scale(send_cwnd,
				alpha_scale_num)/(srtt*srtt);
		if(tmp>max_numerator){
			max_numerator=tmp;
			best_rtt=srtt;
			best_cwnd=send_cwnd;
		}
	}
	send_cwnd=GetCongestionWindow()/kDefaultTCPMSS;
	srtt=get_srtt_us();
	sum_denominator+=mptcp_ccc_scale(send_cwnd,
			alpha_scale_den) * best_rtt/srtt;
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
			LiaSender *sender=(*it);
			send_cwnd=sender->GetCongestionWindow()/kDefaultTCPMSS;
			srtt=sender->get_srtt_us();
			sum_denominator+=mptcp_ccc_scale(send_cwnd,
					alpha_scale_den) * best_rtt/srtt;
	}
	sum_denominator *= sum_denominator;
	CHECK(sum_denominator>0);
	alpha = mptcp_ccc_scale(best_cwnd, alpha_scale_num)/sum_denominator;
	CHECK(alpha>=1);
	alpha_=alpha;
	for(auto it=other_ccs_.begin();it!=other_ccs_.end();it++){
		LiaSender *sender=(*it);
		sender->set_alpha(alpha);
	}
}
}
