#include "received_packet_manager.h"
#include "logging.h"
namespace dqc{
ReceivdPacketManager::ReceivdPacketManager()
:time_largest_observed_(ProtoTime::Zero()){
    ecn_ce_count_=0;
    ack_frame_.ecn_ce_count=ecn_ce_count_;
}
void ReceivdPacketManager::RecordPacketReceived(PacketNumber seq,ProtoTime receive_time){
    if(!ack_frame_updated_){
        ack_frame_.received_packet_times.clear();
    }
    ack_frame_updated_=true;
    if(time_largest_observed_==ProtoTime::Zero()){
        ack_frame_.largest_acked=seq;
        ack_frame_.packets.Add(seq);
        time_largest_observed_=receive_time;
    }else{
        if(seq>LargestAcked(ack_frame_)){
            ack_frame_.packets.Add(seq);
            ack_frame_.largest_acked=seq;
            time_largest_observed_=receive_time;
        }
    }
 if (save_timestamps_) {
    // The timestamp format only handles packets in time order.
    if (!ack_frame_.received_packet_times.empty() &&
        ack_frame_.received_packet_times.back().second > receive_time) {
      LOG(WARNING)<< "Receive time went backwards";
    } else {
      ack_frame_.received_packet_times.push_back(
          std::make_pair(seq, receive_time));
    }
  }
}
const AckFrame &ReceivdPacketManager::GetUpdateAckFrame(ProtoTime &now){
    if(time_largest_observed_==ProtoTime::Zero()){
        ack_frame_.ack_delay_time=TimeDelta::Infinite();
    }else{
        ack_frame_.ack_delay_time=now-time_largest_observed_;
    }
    return ack_frame_;
}
void ReceivdPacketManager::AddEcnCount(uint32_t bytes){
    ecn_ce_count_+=bytes;
    ack_frame_.ecn_ce_count=ecn_ce_count_;
}
void ReceivdPacketManager::ResetAckStates(){
	ack_frame_updated_ = false;
}
void ReceivdPacketManager::DontWaitForPacketsBefore(PacketNumber least_unacked){
    bool update_stop_waiting=false;
    if(0==peer_least_packet_awaiting_ack_.ToUint64()){
        peer_least_packet_awaiting_ack_=least_unacked;
        update_stop_waiting=true;
    }else{
        if(least_unacked>peer_least_packet_awaiting_ack_){
            peer_least_packet_awaiting_ack_=least_unacked;
            update_stop_waiting=true;
        }
    }
    if(update_stop_waiting){
        ack_frame_.packets.RemoveUpTo(least_unacked);
    }
}
}
