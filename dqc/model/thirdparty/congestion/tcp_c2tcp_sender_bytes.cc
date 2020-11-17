#include "tcp_c2tcp_sender_bytes.h"
#include "rtt_stats.h"
#include "logging.h"
#include <algorithm>
#include <cstdint>
#include <string>

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
#define TCP_C2TCP_X_SCALE 100
#define TCP_C2TCP_ALPHA_INIT 150
#define DEFAULT_CODEL_LIMIT 1000
#define REC_INV_SQRT_BITS (8 * sizeof(uint16_t))
#define REC_INV_SQRT_SHIFT (32 - REC_INV_SQRT_BITS)
uint16_t NewtonStep (uint16_t recInvSqrt, uint32_t count)
{
  uint32_t invsqrt = ((uint32_t) recInvSqrt) << REC_INV_SQRT_SHIFT;
  uint32_t invsqrt2 = ((uint64_t) invsqrt * invsqrt) >> 32;
  uint64_t val = (3ll << 32) - ((uint64_t) count * invsqrt2);

  val >>= 2; /* avoid overflow */
  val = (val * invsqrt) >> (32 - 2 + 1);
  return static_cast<uint16_t>(val >> REC_INV_SQRT_SHIFT);
}
/* borrowed from the linux kernel */
static inline uint32_t ReciprocalDivide (uint32_t A, uint32_t R)
{
  return (uint32_t)(((uint64_t)A * R) >> 32);
}
 uint32_t ControlLaw (uint32_t t, uint32_t interval, uint32_t recInvSqrt)
{
  return t + ReciprocalDivide (interval, recInvSqrt << REC_INV_SQRT_SHIFT);
}

}  // namespace

TcpC2tcpSenderBytes::TcpC2tcpSenderBytes(
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
      cubic_(clock),
      num_acked_packets_(0),
      congestion_window_(initial_tcp_congestion_window * kDefaultTCPMSS),
      min_congestion_window_(kDefaultMinimumCongestionWindow),
      max_congestion_window_(max_congestion_window * kDefaultTCPMSS),
      slowstart_threshold_(max_congestion_window * kDefaultTCPMSS),
      initial_tcp_congestion_window_(initial_tcp_congestion_window *
                                     kDefaultTCPMSS),
      initial_max_tcp_congestion_window_(max_congestion_window *
                                         kDefaultTCPMSS),
      min_slow_start_exit_window_(min_congestion_window_) {
          c2tcp_alpha_=TCP_C2TCP_ALPHA_INIT;
      }

TcpC2tcpSenderBytes::~TcpC2tcpSenderBytes() {}
void TcpC2tcpSenderBytes::AdjustNetworkParameters(
    QuicBandwidth bandwidth,
    TimeDelta rtt,
    bool /*allow_cwnd_to_decrease*/) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}

float TcpC2tcpSenderBytes::RenoBeta() const {
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}

void TcpC2tcpSenderBytes::OnCongestionEvent(
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
    C2TcpPacketAcked(event_time,acked_packet.packet_number,prior_in_flight);
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                  prior_in_flight, event_time);
  }
}

void TcpC2tcpSenderBytes::OnPacketAcked(QuicPacketNumber acked_packet_number,
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

void TcpC2tcpSenderBytes::OnPacketSent(
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

bool TcpC2tcpSenderBytes::CanSend(QuicByteCount bytes_in_flight) {
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

QuicBandwidth TcpC2tcpSenderBytes::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  TimeDelta srtt = rtt_stats_->SmoothedOrInitialRtt();
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (no_prr_ && InRecovery() ? 1 : 1.25));
}

QuicBandwidth TcpC2tcpSenderBytes::BandwidthEstimate() const {
  TimeDelta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool TcpC2tcpSenderBytes::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool TcpC2tcpSenderBytes::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool TcpC2tcpSenderBytes::InRecovery() const {
  return largest_acked_packet_number_.IsInitialized() &&
         largest_sent_at_last_cutback_.IsInitialized() &&
         largest_acked_packet_number_ <= largest_sent_at_last_cutback_;
}

bool TcpC2tcpSenderBytes::ShouldSendProbingPacket() const {
  return false;
}

void TcpC2tcpSenderBytes::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_.Clear();
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

std::string TcpC2tcpSenderBytes::GetDebugState() const {
  return "";
}

void TcpC2tcpSenderBytes::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

void TcpC2tcpSenderBytes::SetCongestionWindowFromBandwidthAndRtt(
    QuicBandwidth bandwidth,
    TimeDelta rtt) {
  QuicByteCount new_congestion_window = bandwidth.ToBytesPerPeriod(rtt);
  // Limit new CWND if needed.
  congestion_window_ =
      std::max(min_congestion_window_,
               std::min(new_congestion_window,
                        kMaxResumptionCongestionWindow * kDefaultTCPMSS));
}

void TcpC2tcpSenderBytes::SetInitialCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void TcpC2tcpSenderBytes::SetMinCongestionWindowInPackets(
    QuicPacketCount congestion_window) {
  min_congestion_window_ = congestion_window * kDefaultTCPMSS;
}

void TcpC2tcpSenderBytes::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
  cubic_.SetNumConnections(num_connections_);
}

void TcpC2tcpSenderBytes::ExitSlowstart() {
  slowstart_threshold_ = congestion_window_;
}

void TcpC2tcpSenderBytes::OnPacketLost(QuicPacketNumber packet_number,
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
  } else {
    congestion_window_ =
        cubic_.CongestionWindowAfterPacketLoss(congestion_window_);
  }
  if (congestion_window_ < min_congestion_window_) {
    congestion_window_ = min_congestion_window_;
  }
  slowstart_threshold_ = congestion_window_;
  largest_sent_at_last_cutback_ = largest_sent_packet_number_;
}

QuicByteCount TcpC2tcpSenderBytes::GetCongestionWindow() const {
  return congestion_window_;
}

QuicByteCount TcpC2tcpSenderBytes::GetSlowStartThreshold() const {
  return slowstart_threshold_;
}

// Called when we receive an ack. Normal TCP tracks how many packets one ack
// represents, but quic has a separate ack for each packet.
void TcpC2tcpSenderBytes::MaybeIncreaseCwnd(
    QuicPacketNumber acked_packet_number,
    QuicByteCount acked_bytes,
    QuicByteCount prior_in_flight,
    ProtoTime event_time) {
  //QUIC_BUG_IF(InRecovery()) << "Never increase the CWND during recovery.";
  // Do not increase the congestion window unless the sender is close to using
  // the current window.
  if (!IsCwndLimited(prior_in_flight)) {
    cubic_.OnApplicationLimited();
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
  // Congestion avoidance.
    congestion_window_ = std::min(
        max_congestion_window_,
        cubic_.CongestionWindowAfterAck(acked_bytes, congestion_window_,
                                        rtt_stats_->min_rtt(), event_time));
}

void TcpC2tcpSenderBytes::HandleRetransmissionTimeout() {
  cubic_.ResetCubicState();
  slowstart_threshold_ = congestion_window_ / 2;
  congestion_window_ = min_congestion_window_;
}

void TcpC2tcpSenderBytes::OnConnectionMigration() {
  hybrid_slow_start_.Restart();
  prr_ = PrrSender();
  largest_sent_packet_number_.Clear();
  largest_acked_packet_number_.Clear();
  largest_sent_at_last_cutback_.Clear();
  last_cutback_exited_slowstart_ = false;
  cubic_.ResetCubicState();
  num_acked_packets_ = 0;
  congestion_window_ = initial_tcp_congestion_window_;
  max_congestion_window_ = initial_max_tcp_congestion_window_;
  slowstart_threshold_ = initial_max_tcp_congestion_window_;
}

CongestionControlType TcpC2tcpSenderBytes::GetCongestionControlType() const {
  return  kC2TcpBytes;
}
void TcpC2tcpSenderBytes::C2TcpPacketAcked(ProtoTime event_time,
                    QuicPacketNumber packet_number,
                    QuicByteCount prior_in_flight){
    //if(InSlowStart()||InRecovery()){return;}
    uint64_t rtt_us=rtt_stats_->latest_rtt().ToMicroseconds();
    if(rtt_us==0){return;}
    c2tcp_min_urtt_=rtt_stats_->MinOrInitialRtt().ToMicroseconds();
/*    if(c2tcp_min_urtt_==0||c2tcp_min_urtt_>rtt_us){
        =rtt_us;
    }*/
    uint32_t min_rtt_ms=c2tcp_min_urtt_/1000;
    uint32_t interval,set_point;
    set_point=min_rtt_ms*c2tcp_alpha_/TCP_C2TCP_X_SCALE;
    interval=set_point;
    uint32_t now=(event_time-ProtoTime::Zero()).ToMilliseconds();
    uint32_t rtt_ms=rtt_us/1000;
    if(InRecovery()){
        first_above_time_=0;
        N_=1;	
    }
    if(set_point>rtt_ms){
        first_above_time_=0;
        N_=1;
        uint32_t temp=kDefaultTCPMSS*set_point/rtt_ms;
        //std::cout<<temp<<" "<<congestion_window_<<std::endl;
        num_acked_packets_+=temp;
        if(num_acked_packets_>=congestion_window_){
            congestion_window_ += kDefaultTCPMSS;
            num_acked_packets_=0;
            congestion_window_=std::min(congestion_window_,max_congestion_window_);
        }
    }else if(first_above_time_==0){
        first_above_time_=now+interval;
        next_time_=first_above_time_;
        N_=1;
        rec_inv_sqrt_=~0U >> REC_INV_SQRT_SHIFT;
        rec_inv_sqrt_=NewtonStep(rec_inv_sqrt_,N_);
    }else if(now>next_time_){
        next_time_=ControlLaw(now,interval,rec_inv_sqrt_);
        N_+=1;
        rec_inv_sqrt_=NewtonStep(rec_inv_sqrt_,N_);
        OnPacketLost(packet_number,0,prior_in_flight);
        //TO Test, tp->snd_cwnd = 1;
	congestion_window_=initial_tcp_congestion_window_;
	slowstart_threshold_ = congestion_window_;
        num_acked_packets_=0;
    }
}
}
