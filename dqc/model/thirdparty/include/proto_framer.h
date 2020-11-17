#pragma once
#include "proto_types.h"
#include "byte_codec.h"
#include "proto_time.h"
#include "proto_error_codes.h"
#include "proto_packets.h"
#include "proto_stream_data_producer.h"
namespace dqc{
struct AckFrame;
class ProtoFramer;
class ProtoFrameVisitor{
public:
  virtual ~ProtoFrameVisitor(){}
  virtual bool OnStreamFrame(PacketStream &frame)=0;
  virtual void OnError(ProtoFramer* framer) = 0;
  virtual void OnEcnMarkCount(uint64_t ecn_ce_count)=0;
  // Called when largest acked of an AckFrame has been parsed.
  virtual bool OnAckFrameStart(PacketNumber largest_acked,
                               TimeDelta ack_delay_time) = 0;

  // Called when ack range [start, end) of an AckFrame has been parsed.
  virtual bool OnAckRange(PacketNumber start, PacketNumber end) = 0;

  // Called when a timestamp in the AckFrame has been parsed.
  virtual bool OnAckTimestamp(PacketNumber packet_number,
                              ProtoTime timestamp) = 0;

  // Called after the last ack range in an AckFrame has been parsed.
  // |start| is the starting value of the last ack range.
  virtual bool OnAckFrameEnd(PacketNumber start) = 0;
  virtual bool OnStopWaitingFrame(const PacketNumber least_unacked){return false;}
};
class ProtoFramer{
public:
  bool ProcessStopWaitingFrame(basic::DataReader* reader,
                               const ProtoPacketHeader& header,
                               PacketNumber* least_unacked);
  bool AppendStopWaitingFrame(const ProtoPacketHeader& header,
                              const PacketNumber& least_unacked,
                              basic::DataWriter* writer);
  bool AppendAckFrameAndTypeByte(const AckFrame& frame,basic::DataWriter *writer);
  bool AppendStreamFrame(const PacketStream& frame,
                         bool no_stream_frame_length,
                         basic::DataWriter* writer);
  bool ProcessFrameData(basic::DataReader* reader, const ProtoPacketHeader& header);
  struct AckFrameInfo {
    AckFrameInfo();
    AckFrameInfo(const AckFrameInfo& other)=default;
    ~AckFrameInfo(){}

    // The maximum ack block length.
    PacketCount max_block_length;
    // Length of first ack block.
    PacketCount first_block_length;
    // Number of ACK blocks needed for the ACK frame.
    size_t num_ack_blocks;
  };
  static AckFrameInfo GetAckFrameInfo(const AckFrame& frame);
  static bool AppendPacketNumber(ProtoPacketNumberLength packet_number_length,
                                 PacketNumber packet_number,
                                 basic::DataWriter* writer);
  // Appends a single ACK block to |writer| and returns true if the block was
  // successfully appended.
  static bool AppendAckBlock(uint8_t gap,
                             ProtoPacketNumberLength length_length,
                             uint64_t length,
                             basic::DataWriter* writer);
    bool AppendTimestampsToAckFrame (const AckFrame& frame,basic::DataWriter* writer);
    uint8_t GetStreamFrameTypeByte(const PacketStream& frame,
                                   bool last_frame_in_packet) const;
  // Allows enabling or disabling of timestamp processing and serialization.
    
  void set_process_timestamps(bool process_timestamps) {
    process_timestamps_ = process_timestamps;
  }
  void set_data_producer(ProtoStreamDataProducer *data_producer){
    data_producer_=data_producer;
  }
  void set_visitor(ProtoFrameVisitor *visitor){
    visitor_=visitor;
  }
private:
    bool ProcessStreamFrame(basic::DataReader* reader,
                            uint8_t frame_type,
                            PacketStream* frame);
    bool ProcessAckFrame(basic::DataReader* reader, uint8_t frame_type);
    bool ProcessTimestampsInAckFrame(uint8_t num_received_packets,
                                   PacketNumber largest_acked,
                                   basic::DataReader* reader);
    TimeDelta CalculateTimestampFromWire(uint32_t time_delta_us);                               
    void set_detailed_error(const char* error) { detailed_error_ = error; }
    bool RaiseError(ProtoErrorCode error);
    bool process_timestamps_{false};
    ProtoTime creation_time_{ProtoTime::Zero()};
    TimeDelta last_timestamp_{TimeDelta::Zero()};
    ProtoFrameVisitor *visitor_{nullptr};
    ProtoStreamDataProducer *data_producer_{nullptr};
    const char *detailed_error_{nullptr};
};
size_t GetMinAckFrameSize(ProtoPacketNumberLength largest_observed_length);
size_t GetAckFrameTimeStampSize(const AckFrame& ack);
size_t GetStreamIdSize(uint32_t stream_id);
size_t GetStreamOffsetSize(StreamOffset offset);
bool AppendStreamId(size_t stream_id_length,
                    uint32_t stream_id,
                    basic::DataWriter* writer);
bool AppendStreamOffset(size_t offset_length,
                        StreamOffset offset,
                        basic::DataWriter* writer);
bool AppendPacketHeader(ProtoPacketHeader& header,basic::DataWriter *writer);
bool ProcessPacketHeader(basic::DataReader* reader,ProtoPacketHeader& header);
}//namespace dqc;
