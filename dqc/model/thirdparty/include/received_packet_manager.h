#ifndef RECEIVED_PACKET_MANAGER_H_
#define RECEIVED_PACKET_MANAGER_H_
#include "ack_frame.h"
#include "proto_time.h"
namespace dqc{
class ReceivdPacketManager{
public:
    ReceivdPacketManager();
    ~ReceivdPacketManager(){}
    void RecordPacketReceived(PacketNumber seq,ProtoTime receive_time);
    const AckFrame &GetUpdateAckFrame(ProtoTime &now);
  // Deletes all missing packets before least unacked. The connection won't
  // process any packets with packet number before |least_unacked| that it
  // received after this call.
    void DontWaitForPacketsBefore(PacketNumber least_unacked);
    void set_save_timestamps(bool save_timestamps) {
        save_timestamps_ = save_timestamps;
    }
    void AddEcnCount(uint32_t bytes);
	void ResetAckStates();
private:
    bool save_timestamps_{false};
    bool ack_frame_updated_{false};
    ProtoTime time_largest_observed_;
    AckFrame ack_frame_;
    PacketNumber peer_least_packet_awaiting_ack_{0};
    uint64_t ecn_ce_count_{0};
};

}//namespace dqc
#endif
