#pragma once
#include "proto_packets.h"
#include "unacked_packet_map.h"
#include "linked_hash_map.h"
#include "ack_frame.h"
#include "proto_pacing_sender.h"
#include "rtt_stats.h"
#include "proto_connection_stats.h"
#include "random.h"
#include <functional>
namespace dqc{
using TraceLossPacketDelay=std::function<void(PacketNumber,uint32_t)>;
class SendPacketManager{
public:
    SendPacketManager(ProtoClock *clock,QuicConnectionStats* stats,StreamAckedObserver *acked_observer);
    ~SendPacketManager();
  // Sets the send algorithm to the given congestion control type and points the
  // pacing sender at |send_algorithm_|. Can be called any number of times.
    void SetSendAlgorithm(CongestionControlType congestion_control_type);
    void SetMaxPacingRate(QuicBandwidth max_pacing_rate) {
    pacing_sender_.set_max_pacing_rate(max_pacing_rate);
    }
    QuicBandwidth PacingRate()const {
        return pacing_sender_.PacingRate(0);
    }
    QuicBandwidth BandwidthEstimate() const{
    	return send_algorithm_->BandwidthEstimate();
    }
	RttStats *GetRttStats(){ return &rtt_stats_;}
	PacketNumber largest_acked() const {return largest_acked_;}
	void InFlight(ByteCount *inflight,ByteCount *cwnd) {
	 *inflight=unacked_packets_.bytes_in_flight();
	 *cwnd=send_algorithm_->GetCongestionWindow();
	}
	bool CheckCanSend() const {return send_algorithm_->CanSend(unacked_packets_.bytes_in_flight());}
  // Sets the send algorithm to |send_algorithm| and points the pacing sender at
  // |send_algorithm_|. Takes ownership of |send_algorithm|. Can be called any
  // number of times.
  // Setting the send algorithm once the connection is underway is dangerous.
    void SetSendAlgorithm(SendAlgorithmInterface* send_algorithm);
    bool OnSentPacket(SerializedPacket *packet,PacketNumber old,
                      HasRetransmittableData has_retrans,ProtoTime send_ts);
    const TimeDelta TimeUntilSend(ProtoTime now) const;
    typedef linked_hash_map<PacketNumber,TransmissionInfo,QuicPacketNumberHash> PendingRetransmissionMap;
    PacketNumber GetLeastUnacked(){ return unacked_packets_.GetLeastUnacked();}
    bool HasPendingForRetrans();
    PendingRetransmission NextPendingRetrans();
    void Retransmitted(PacketNumber number);
    //TODO handle rto
    void OnRetransmissionTimeOut();
    bool NeedFastRetrans() {
    	bool need_retrans=fast_retrans_flag_;
    	fast_retrans_flag_=false;
    	return need_retrans;
    }
    void FastRetransmit();
    int DeliverPacketsToPendingQueue(int n);
    void UpdateEcnBytes(uint64_t ecn_ce_count);
    void OnAckStart(PacketNumber largest_acked,TimeDelta ack_delay_time,ProtoTime ack_receive_time);
    void OnAckRange(PacketNumber start,PacketNumber end);
    void OnAckTimestamp(PacketNumber packet_number,ProtoTime timestamp);
    AckResult OnAckEnd(ProtoTime ack_receive_time);
    void MaybeInvokeCongestionEvent(bool rtt_updated,
                                  ByteCount prior_in_flight,
                                  ProtoTime event_time);
    bool has_in_flight(){
    	return unacked_packets_.bytes_in_flight()>0;
    }
    void Test();
    void Test2();
    const TimeDelta GetRetransmissionDelay() const{
        return GetRetransmissionDelay(consecutive_rto_count_);
    }
    const TimeDelta GetRetransmissionDelay(size_t consecutive_rto_count) const;
    void SetTraceLossPacketDelay(TraceLossPacketDelay cb){trace_lost_=std::move(cb);}
    std::pair<PacketNumber,TimeDelta> GetOneWayDelayInfo() { return one_way_delay_;}
    void SetCongestionId(uint32_t cid);
	void SetNumEmulatedConnections(int num_connections);
private:
      // Update the RTT if the ack is for the largest acked packet number.
  // Returns true if the rtt was updated.
    bool MaybeUpdateRTT(PacketNumber largest_acked,
                      TimeDelta ack_delay_time,
                      ProtoTime ack_receive_time);
    // Called after packets have been marked handled with last received ack frame.
    void PostProcessNewlyAckedPackets(ProtoTime ack_receive_time,
                                      bool rtt_updated,
                                      ByteCount prior_bytes_in_flight);
    void InvokeLossDetection(ProtoTime time);
    void MarkForRetrans(PacketNumber seq);
    void PostToPending(PacketNumber seq,TransmissionInfo &info);
    void ClearAckedAndLossVector();
    ProtoClock *clock_{nullptr};
	QuicConnectionStats* stats_;
    StreamAckedObserver *acked_observer_{nullptr};
    UnackedPacketMap unacked_packets_;
    PendingRetransmissionMap pendings_;
    PacketNumber largest_acked_;
    LostPacketVector packets_lost_;
    AckedPacketVector packets_acked_;
    AckFrame last_ack_frame_;
    PacketQueue::const_reverse_iterator ack_packet_itor_;
    bool using_pacing_{true};
    bool rtt_updated_{false};
    RttStats rtt_stats_;
    size_t consecutive_rto_count_{0};
    TimeDelta min_rto_timeout_;
    PacingSender pacing_sender_;
    Random rand_;
    std::unique_ptr<SendAlgorithmInterface> send_algorithm_{nullptr};
    bool fast_retrans_flag_{false};
    TraceLossPacketDelay trace_lost_;
    ProtoTime sent_time_{ProtoTime::Zero()};
    ProtoTime recv_time_{ProtoTime::Zero()};
    std::pair<PacketNumber,TimeDelta> one_way_delay_;
};
}//namespace dqc;
