#include <limits>
#include "proto_utils.h"
#include "proto_framer.h"
#include "ack_frame.h"
#include "logging.h"
#include "proto_comm.h"
#include "string_utils.h"
#define QUIC_PREDICT_FALSE_IMPL(x) x
#define QUIC_PREDICT_FALSE(x) QUIC_PREDICT_FALSE_IMPL(x)
namespace dqc{
// public header;
const uint8_t kPublicHeaderSequenceNumberShift = 4;
// Number of bytes reserved for the frame type preceding each frame.
const size_t kFrameTypeSize = 1;
// Size in bytes reserved for the delta time of the largest observed
// packet number in ack frames.
const size_t kDeltaTimeLargestObservedSize = 2;
// Size in bytes reserved for the number of received packets with timestamps.
const size_t kNumTimestampsSize = 1;
// Size in bytes reserved for the number of ack blocks in ack frames.
const size_t kNumberOfAckBlocksSize = 1;
// Acks may have only one ack block.
const uint8_t kQuicHasMultipleAckBlocksOffset = 5;
// packet number size shift used in AckFrames.
const uint8_t kQuicSequenceNumberLengthNumBits = 2;
const uint8_t kActBlockLengthOffset = 0;
const uint8_t kLargestAckedOffset = 2;

const uint8_t kQuicFrameTypeStreamMask = 0x80;
const uint8_t kQuicFrameTypeAckMask = 0x40;


// Timestamps are 4 bytes followed by 2 bytes.
const uint8_t kQuicNumTimestampsLength = 1;
const uint8_t kQuicFirstTimestampLength = 4;
const uint8_t kQuicTimestampLength = 2;
// Gaps between packet numbers are 1 byte.
const uint8_t kQuicTimestampPacketNumberGapLength = 1;


// The stream type format is 1FDOOOSS, where
//    F is the fin bit.
//    D is the data length bit (0 or 2 bytes).
//    OO/OOO are the size of the offset.
//    SS is the size of the stream ID.
// Note that the stream encoding can not be determined by inspection. It can
// be determined only by knowing the QUIC Version.
// Stream frame relative shifts and masks for interpreting the stream flags.
// StreamID may be 1, 2, 3, or 4 bytes.
const uint8_t kQuicStreamIdShift = 2;
const uint8_t kQuicStreamIDLengthMask = 0x03;

// Offset may be 0, 2, 4, or 8 bytes.
const uint8_t kQuicStreamShift = 3;
const uint8_t kQuicStreamOffsetMask = 0x07;

// Data length may be 0 or 2 bytes.
const uint8_t kQuicStreamDataLengthShift = 1;
const uint8_t kQuicStreamDataLengthMask = 0x01;

// Fin bit may be set or not.
const uint8_t kQuicStreamFinShift = 1;
const uint8_t kQuicStreamFinMask = 0x01;

// Create a mask that sets the last |num_bits| to 1 and the rest to 0.
inline uint8_t GetMaskFromNumBits(uint8_t num_bits) {
  return (1u << num_bits) - 1;
}
// Extract |num_bits| from |flags| offset by |offset|.
uint8_t ExtractBits(uint8_t flags, uint8_t num_bits, uint8_t offset) {
  return (flags >> offset) & GetMaskFromNumBits(num_bits);
}

// Extract the bit at position |offset| from |flags| as a bool.
bool ExtractBit(uint8_t flags, uint8_t offset) {
  return ((flags >> offset) & GetMaskFromNumBits(1)) != 0;
}
static void SetBits(uint8_t* flags, uint8_t val, uint8_t num_bits, uint8_t offset){
  DCHECK_LE(val, GetMaskFromNumBits(num_bits));
  *flags |= val << offset;
}
// Set the bit at position |offset| to |val| in |flags|.
void SetBit(uint8_t* flags, bool val, uint8_t offset) {
  SetBits(flags, val ? 1 : 0, 1, offset);
}
size_t GetMinAckFrameSize(ProtoPacketNumberLength largest_observed_length){
  size_t min_size = kFrameTypeSize + largest_observed_length +
                    kDeltaTimeLargestObservedSize;
  return min_size + kNumTimestampsSize;
}
ProtoPacketNumberLength ReadAckPacketNumberLength(uint8_t flag){
    switch(flag&PACKET_FLAG_4BYTE){
    case PACKET_FLAG_1BYTE:{
        return PACKET_NUMBER_1BYTE;
    }
    case PACKET_FLAG_2BYTE:{
        return PACKET_NUMBER_2BYTE;
    }
    case PACKET_FLAG_3BYTE:{
        return PACKET_NUMBER_3BYTE;
    }
    case PACKET_FLAG_4BYTE:{
        return PACKET_NUMBER_4BYTE;
    }
    }
    return PACKET_NUMBER_6BYTE;
}
size_t GetAckFrameTimeStampSize(const AckFrame& ack) {
  if (ack.received_packet_times.empty()) {
    return 0;
  }

  return kQuicNumTimestampsLength + kQuicFirstTimestampLength +
         (kQuicTimestampLength + kQuicTimestampPacketNumberGapLength) *
             (ack.received_packet_times.size() - 1);
}
size_t GetStreamIdSize(uint32_t stream_id){
  // Sizes are 1 through 4 bytes.
  for (int i = 1; i <= 4; ++i) {
    stream_id >>= 8;
    if (stream_id == 0) {
      return i;
    }
  }
  DLOG(FATAL)<< "Failed to determine StreamID Size.";
  return 4;
}
size_t GetStreamOffsetSize(StreamOffset offset){
  // 0 is a special case.
  if (offset == 0) {
    return 0;
  }
  // 2 through 8 are the remaining sizes.
  offset >>= 8;
  for (int i = 2; i <= 8; ++i) {
    offset >>= 8;
    if (offset == 0) {
      return i;
    }
  }
  DLOG(FATAL)<< "Failed to determine StreamOffsetSize.";
  return 8;
}
bool AppendPacketHeader(ProtoPacketHeader& header,basic::DataWriter *writer){
    header.packet_number_length=GetMinPktNumLen(header.packet_number);
    uint8_t public_flags=0;
    uint32_t seq_len=header.packet_number_length;
    public_flags|=(PktNumLen2Flag(header.packet_number_length)
                   <<kPublicHeaderSequenceNumberShift);
    if(writer->WriteBytes(&public_flags,1)){
        return writer->WriteBytesToUInt64(seq_len,header.packet_number.ToUint64());
    }
    return false;
}
bool ProcessPacketHeader(basic::DataReader* reader,ProtoPacketHeader& header){
    uint8_t public_flags=0;
    bool ret=false;
    if(reader->ReadBytes(&public_flags,1)){
        header.packet_number_length=ReadPacketNumberLength(public_flags >> kPublicHeaderSequenceNumberShift);
        uint32_t seq_len=header.packet_number_length;
        uint64_t seq=0;
        ret= reader->ReadBytesToUInt64(seq_len,&seq);
        header.packet_number=PacketNumber(seq);
    }
    return ret;
}
bool AppendStreamId(size_t stream_id_length,
                    uint32_t stream_id,
                    basic::DataWriter* writer){
  if (stream_id_length == 0 || stream_id_length > 4) {
    DLOG(FATAL)<< "Invalid stream_id_length: " << stream_id_length;
    return false;
  }
  return writer->WriteBytesToUInt64(stream_id_length, stream_id);
}
bool AppendStreamOffset(size_t offset_length,
                        StreamOffset offset,
                        basic::DataWriter* writer){
  if (offset_length == 1 || offset_length > 8) {
    DLOG(FATAL)<< "Invalid stream_offset_length: " << offset_length;
    return false;
  }

  return writer->WriteBytesToUInt64(offset_length, offset);
}
// Returns the absolute value of the difference between |a| and |b|.
uint64_t Delta(uint64_t a, uint64_t b) {
  // Since these are unsigned numbers, we can't just return abs(a - b)
  if (a < b) {
    return b - a;
  }
  return a - b;
}

uint64_t ClosestTo(uint64_t target, uint64_t a, uint64_t b) {
  return (Delta(target, a) < Delta(target, b)) ? a : b;
}
uint8_t ProtoFramer::GetStreamFrameTypeByte(const PacketStream& frame,
                                   bool last_frame_in_packet) const{
  uint8_t type_byte = 0;
  // Fin bit.
  type_byte |= frame.fin ? kQuicStreamFinMask : 0;

  // Data Length bit.
  type_byte <<= kQuicStreamDataLengthShift;
  type_byte |= last_frame_in_packet ? 0 : kQuicStreamDataLengthMask;

  // Offset 3 bits.
  type_byte <<= kQuicStreamShift;
  const size_t offset_len =
      GetStreamOffsetSize(frame.offset);
  if (offset_len > 0) {
    type_byte |= offset_len - 1;
  }

  // stream id 2 bits.
  type_byte <<= kQuicStreamIdShift;
  type_byte |= GetStreamIdSize(frame.stream_id) - 1;
  type_byte |= kQuicFrameTypeStreamMask;  // Set Stream Frame Type to 1.

  return type_byte;
}
ProtoFramer::AckFrameInfo::AckFrameInfo()
:max_block_length(0)
,first_block_length(0)
,num_ack_blocks(0){
}
ProtoFramer::AckFrameInfo ProtoFramer::GetAckFrameInfo(const AckFrame& frame){
  AckFrameInfo new_ack_info;
  if (frame.packets.Empty()) {
    return new_ack_info;
  }
  // The first block is the last interval. It isn't encoded with the gap-length
  // encoding, so skip it.
  new_ack_info.first_block_length = frame.packets.LastIntervalLength();
  auto itr = frame.packets.rbegin();
  PacketNumber previous_start = itr->Min();
  new_ack_info.max_block_length = itr->Length();
  ++itr;

  // Don't do any more work after getting information for 256 ACK blocks; any
  // more can't be encoded anyway.
  for (; itr != frame.packets.rend() &&
         new_ack_info.num_ack_blocks < std::numeric_limits<uint8_t>::max();
       previous_start = itr->Min(), ++itr) {
    const auto& interval = *itr;
    const PacketCount total_gap = previous_start - interval.Max();
    new_ack_info.num_ack_blocks +=
        (total_gap + std::numeric_limits<uint8_t>::max() - 1) /
        std::numeric_limits<uint8_t>::max();
    new_ack_info.max_block_length =
        std::max(new_ack_info.max_block_length, interval.Length());
  }
  return new_ack_info;
}
bool ProtoFramer::AppendStreamFrame(const PacketStream& frame,
                                    bool no_stream_frame_length,
                                    basic::DataWriter* writer){

  if (!AppendStreamId(GetStreamIdSize(frame.stream_id), frame.stream_id,
                      writer)) {
    DLOG(INFO)<< "Writing stream id size failed.";
    return false;
  }
  if (!AppendStreamOffset(
          GetStreamOffsetSize(frame.offset),
          frame.offset, writer)) {
    DLOG(INFO)<< "Writing offset size failed.";
    return false;
  }
  if (!no_stream_frame_length) {
    if ((frame.len> std::numeric_limits<uint16_t>::max()) ||
        !writer->WriteUInt16(static_cast<uint16_t>(frame.len))) {
      DLOG(INFO)<< "Writing stream frame length failed";
      return false;
    }
  }

  if (data_producer_ != nullptr) {
    DCHECK_EQ(nullptr, frame.data_buffer);
    if (frame.len== 0) {
      return true;
    }
    if (data_producer_->WriteStreamData(frame.stream_id, frame.offset,
                                        frame.len,
                                        writer) != true) {
      DLOG(FATAL)<< "Writing frame data failed.";
      return false;
    }
    return true;
  }
  if (!writer->WriteBytes(frame.data_buffer, frame.len)) {
    DLOG(FATAL)<< "Writing frame data failed.";
    return false;
  }
    return true;
}
bool ProtoFramer::AppendPacketNumber(ProtoPacketNumberLength packet_number_length,
                                 PacketNumber packet_number,
                                 basic::DataWriter* writer){
  size_t length = packet_number_length;
  if (length != 1 && length != 2 && length!=3&& length != 4 && length != 6 && length != 8) {
    DLOG(FATAL)<< "Invalid packet_number_length: " << length;
    return false;
  }
  return writer->WriteBytesToUInt64(packet_number_length, packet_number.ToUint64());
}
bool ProtoFramer::AppendAckBlock(uint8_t gap,
                             ProtoPacketNumberLength length_length,
                             uint64_t length,
                             basic::DataWriter* writer){
  return writer->WriteUInt8(gap) &&
         AppendPacketNumber(length_length, PacketNumber(length), writer);
}
bool ProtoFramer::AppendAckFrameAndTypeByte(const AckFrame& frame,basic::DataWriter *writer){
  const AckFrameInfo new_ack_info = GetAckFrameInfo(frame);
  PacketNumber largest_acked = LargestAcked(frame);
  ProtoPacketNumberLength largest_acked_length =
      GetMinPktNumLen(largest_acked);
  ProtoPacketNumberLength ack_block_length = GetMinPktNumLen(
    PacketNumber(new_ack_info.max_block_length));
  // Calculate available bytes for timestamps and ack blocks.
  int32_t available_timestamp_and_ack_block_bytes =
      writer->capacity() - writer->length() - ack_block_length -
      GetMinAckFrameSize(largest_acked_length) -
      (new_ack_info.num_ack_blocks != 0 ? kNumberOfAckBlocksSize : 0);
  DCHECK_LE(0, available_timestamp_and_ack_block_bytes);

  // Write out the type byte by setting the low order bits and doing shifts
  // to make room for the next bit flags to be set.
  // Whether there are multiple ack blocks.
  uint8_t type_byte = 0;
  SetBit(&type_byte, new_ack_info.num_ack_blocks != 0,
         kQuicHasMultipleAckBlocksOffset);

  SetBits(&type_byte, PktNumLen2Flag(largest_acked_length),
          kQuicSequenceNumberLengthNumBits, kLargestAckedOffset);

  SetBits(&type_byte, PktNumLen2Flag(ack_block_length),
          kQuicSequenceNumberLengthNumBits, kActBlockLengthOffset);

  type_byte |= kQuicFrameTypeAckMask;

  if (!writer->WriteUInt8(type_byte)) {
    return false;
  }
  if (!writer->WriteVarInt62(frame.ecn_ce_count)) {
    return false;
  }  
  size_t max_num_ack_blocks = available_timestamp_and_ack_block_bytes /
                              (ack_block_length + PACKET_NUMBER_1BYTE);
  // Number of ack blocks.
  size_t num_ack_blocks =
      std::min(new_ack_info.num_ack_blocks, max_num_ack_blocks);
  if (num_ack_blocks > std::numeric_limits<uint8_t>::max()) {
    num_ack_blocks = std::numeric_limits<uint8_t>::max();
  }
  // Largest acked.
  if (!AppendPacketNumber(largest_acked_length, largest_acked, writer)) {
    return false;
  }
  // Largest acked delta time.
  uint64_t ack_delay_time_us = basic::kUFloat16MaxValue;
  if (!frame.ack_delay_time.IsInfinite()) {
    DCHECK_LE(0u, frame.ack_delay_time.ToMicroseconds());
    ack_delay_time_us = frame.ack_delay_time.ToMicroseconds();
  }
  if (!writer->WriteUFloat16(ack_delay_time_us)) {
    return false;
  }
  if (num_ack_blocks > 0) {
    if (!writer->WriteBytes(&num_ack_blocks, 1)) {
      return false;
    }
  }
  // First ack block length.
  if (!AppendPacketNumber(ack_block_length, PacketNumber(new_ack_info.first_block_length),
                          writer)) {
    return false;
  }
  // Ack blocks.
  if (num_ack_blocks > 0) {
    size_t num_ack_blocks_written = 0;
    // Append, in descending order from the largest ACKed packet, a series of
    // ACK blocks that represents the successfully acknoweldged packets. Each
    // appended gap/block length represents a descending delta from the previous
    // block. i.e.:
    // |--- length ---|--- gap ---|--- length ---|--- gap ---|--- largest ---|
    // For gaps larger than can be represented by a single encoded gap, a 0
    // length gap of the maximum is used, i.e.:
    // |--- length ---|--- gap ---|- 0 -|--- gap ---|--- largest ---|
    auto itr = frame.packets.rbegin();
    PacketNumber previous_start = itr->min();
    ++itr;

    for (;
         itr != frame.packets.rend() && num_ack_blocks_written < num_ack_blocks;
         previous_start = itr->min(), ++itr) {
      const auto& interval = *itr;
      const PacketCount total_gap = previous_start - interval.max();
      const size_t num_encoded_gaps =
          (total_gap + std::numeric_limits<uint8_t>::max() - 1) /
          std::numeric_limits<uint8_t>::max();
      DCHECK_LE(0u, num_encoded_gaps);

      // Append empty ACK blocks because the gap is longer than a single gap.
      for (size_t i = 1;
           i < num_encoded_gaps && num_ack_blocks_written < num_ack_blocks;
           ++i) {
        if (!AppendAckBlock(std::numeric_limits<uint8_t>::max(),
                            ack_block_length, 0, writer)) {
          return false;
        }
        ++num_ack_blocks_written;
      }
      if (num_ack_blocks_written >= num_ack_blocks) {
        if (QUIC_PREDICT_FALSE(num_ack_blocks_written != num_ack_blocks)) {
          DLOG(FATAL)<< "Wrote " << num_ack_blocks_written
                   << ", expected to write " << num_ack_blocks;
        }
        break;
      }

      const uint8_t last_gap =
          total_gap -
          (num_encoded_gaps - 1) * std::numeric_limits<uint8_t>::max();
      // Append the final ACK block with a non-empty size.
      if (!AppendAckBlock(last_gap, ack_block_length, interval.Length(),
                          writer)) {
        return false;
      }
      ++num_ack_blocks_written;
    }
    DCHECK_EQ(num_ack_blocks, num_ack_blocks_written);
  }

  // Timestamps.
  // If we don't process timestamps or if we don't have enough available space
  // to append all the timestamps, don't append any of them.
  if (process_timestamps_ && writer->capacity() - writer->length() >=
                                 GetAckFrameTimeStampSize(frame)) {
    if (!AppendTimestampsToAckFrame(frame, writer)) {
      return false;
    }
  } else {
    uint8_t num_received_packets = 0;
    if (!writer->WriteBytes(&num_received_packets, 1)) {
      return false;
    }
  }
    return true;
}
bool ProtoFramer::AppendTimestampsToAckFrame (const AckFrame& frame,basic::DataWriter* writer){
  DCHECK_GE(std::numeric_limits<uint8_t>::max(),
            frame.received_packet_times.size());
  // num_received_packets is only 1 byte.
  if (frame.received_packet_times.size() >
      std::numeric_limits<uint8_t>::max()) {
    return false;
  }

  uint8_t num_received_packets = frame.received_packet_times.size();
  if (!writer->WriteBytes(&num_received_packets, 1)) {
    return false;
  }
  if (num_received_packets == 0) {
    return true;
  }
  auto it = frame.received_packet_times.begin();
  PacketNumber packet_number = it->first;
  uint8_t delta_from_largest_observed =
      LargestAcked(frame) - packet_number;
  DCHECK_GE(std::numeric_limits<uint8_t>::max(), delta_from_largest_observed);
  if (delta_from_largest_observed > std::numeric_limits<uint8_t>::max()) {
    return false;
  }
  if (!writer->WriteUInt8(delta_from_largest_observed)) {
    return false;
  }
  // Use the lowest 4 bytes of the time delta from the creation_time_.
  const uint64_t time_epoch_delta_us = UINT64_C(1) << 32;
  uint32_t time_delta_us =
      static_cast<uint32_t>((it->second - creation_time_).ToMicroseconds() &
                            (time_epoch_delta_us - 1));
  if (!writer->WriteUInt32(time_delta_us)) {
    return false;
  }
  ProtoTime prev_time = it->second;

  for (++it; it != frame.received_packet_times.end(); ++it) {
    packet_number = it->first;
    delta_from_largest_observed = LargestAcked(frame) - packet_number;

    if (delta_from_largest_observed > std::numeric_limits<uint8_t>::max()) {
      return false;
    }

    if (!writer->WriteUInt8(delta_from_largest_observed)) {
      return false;
    }

    uint64_t frame_time_delta_us = (it->second - prev_time).ToMicroseconds();
    prev_time = it->second;
    if (!writer->WriteUFloat16(frame_time_delta_us)) {
      return false;
    }
  }
  return true;
}
bool ProtoFramer::RaiseError(ProtoErrorCode error){
  DLOG(INFO) << "Error: " << ProtoErrorCodeToString(error)
                  << " detail: " << detailed_error_;
  if(visitor_){
        visitor_->OnError(this);
  }
  return false;
}
bool ProtoFramer::ProcessFrameData(basic::DataReader* reader, const ProtoPacketHeader& header){
  if (reader->IsDoneReading()) {
    set_detailed_error("Packet has no frames.");
    return RaiseError(PROTO_MISSING_PAYLOAD);
  }

  while (!reader->IsDoneReading()){
    uint8_t frame_type;
    if (!reader->ReadBytes(&frame_type, 1)) {
      set_detailed_error("Unable to read frame type.");
      return RaiseError(PROTO_INVALID_FRAME_DATA);
    }
    const uint8_t special_mask=0xE0;
    if (frame_type & special_mask) {
      // Stream Frame
      if (frame_type & kQuicFrameTypeStreamMask) {
        PacketStream frame;
        if (!ProcessStreamFrame(reader, frame_type, &frame)) {
          return RaiseError(PROTO_INVALID_STREAM_DATA);
        }
        if (!visitor_->OnStreamFrame(frame)) {
           DLOG(WARNING)<<"Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      // Ack Frame

      if (frame_type & kQuicFrameTypeAckMask) {
        if (!ProcessAckFrame(reader, frame_type)) {
          return RaiseError(PROTO_INVALID_ACK_DATA);
        }
        continue;
      }

      // This was a special frame type that did not match any
      // of the known ones. Error.
      set_detailed_error("Illegal frame type.");
      DLOG(WARNING) << "Illegal frame type: "
                         << static_cast<int>(frame_type);
      return RaiseError(PROTO_INVALID_FRAME_DATA);
    }


    switch(frame_type){
        case PROTO_FRAME_STOP_WAITING:{
            PacketNumber least_unacked;
            if(!ProcessStopWaitingFrame(reader,header,&least_unacked)){
                set_detailed_error("stop  waitting error.");
                return RaiseError(PROTO_INVALID_STOP_WAITING_DATA);
            }
            if(!visitor_->OnStopWaitingFrame(least_unacked)){
                return true;
            }
            break;
        }
        default:{
            DLOG(INFO)<<"wtf";
            return RaiseError(PROTO_INVALID_FRAME_DATA);
        }
    }
  }
  return true;
}
bool ProtoFramer::ProcessStreamFrame(basic::DataReader* reader,
                                     uint8_t frame_type,
                                     PacketStream* frame){
  uint8_t stream_flags = frame_type;

  uint8_t stream_id_length = 0;
  uint8_t offset_length = 4;
  bool has_data_length = true;
  stream_flags &= ~kQuicFrameTypeStreamMask;

  // Read from right to left: StreamID, Offset, Data Length, Fin.
  stream_id_length = (stream_flags & kQuicStreamIDLengthMask) + 1;
  stream_flags >>= kQuicStreamIdShift;

  offset_length = (stream_flags & kQuicStreamOffsetMask);
  // There is no encoding for 1 byte, only 0 and 2 through 8.
  if (offset_length > 0) {
    offset_length += 1;
  }
  stream_flags >>= kQuicStreamShift;

  has_data_length =
      (stream_flags & kQuicStreamDataLengthMask) == kQuicStreamDataLengthMask;
  stream_flags >>= kQuicStreamDataLengthShift;

  frame->fin = (stream_flags & kQuicStreamFinMask) == kQuicStreamFinShift;

  uint64_t stream_id;
  if (!reader->ReadBytesToUInt64(stream_id_length, &stream_id)) {
    set_detailed_error("Unable to read stream_id.");
    return false;
  }
  frame->stream_id = static_cast<uint32_t>(stream_id);
  if (!reader->ReadBytesToUInt64(offset_length, &frame->offset)) {
    set_detailed_error("Unable to read offset.");
    return false;
  }
  // TODO(ianswett): Don't use QuicStringPiece as an intermediary.
  std::string data;
  if (has_data_length) {
    if (!reader->ReadStringPiece16(&data)) {
      set_detailed_error("Unable to read frame data.");
      return false;
    }
  } else {
    if (!reader->ReadStringPiece(&data, reader->BytesRemaining())) {
      set_detailed_error("Unable to read frame data.");
      return false;
    }
  }
  frame->data_buffer =data.data();
  frame->len= static_cast<uint16_t>(data.length());

  return true;
}
bool ProtoFramer::ProcessAckFrame(basic::DataReader* reader, uint8_t frame_type){
  const bool has_ack_blocks =
      ExtractBit(frame_type, kQuicHasMultipleAckBlocksOffset);
  uint8_t num_ack_blocks = 0;
  uint8_t num_received_packets = 0;

  // Determine the two lengths from the frame type: largest acked length,
  // ack block length.
  const ProtoPacketNumberLength ack_block_length = ReadAckPacketNumberLength(
      ExtractBits(frame_type, kQuicSequenceNumberLengthNumBits,
                  kActBlockLengthOffset));
  const ProtoPacketNumberLength largest_acked_length = ReadAckPacketNumberLength(
      ExtractBits(frame_type, kQuicSequenceNumberLengthNumBits,
                  kLargestAckedOffset));
  uint64_t ecn_ce_count=0;
  if (!reader->ReadVarInt62(&ecn_ce_count)) {
    set_detailed_error("ecn decoder errro");
    return false;
  }
  visitor_->OnEcnMarkCount(ecn_ce_count);
  uint64_t largest_acked;
  if (!reader->ReadBytesToUInt64(largest_acked_length, &largest_acked)) {
    set_detailed_error("Unable to read largest acked.");
    return false;
  }
  uint64_t ack_delay_time_us;
  if (!reader->ReadUFloat16(&ack_delay_time_us)) {
    set_detailed_error("Unable to read ack delay time.");
    return false;
  }

  if (!visitor_->OnAckFrameStart(
          PacketNumber(largest_acked),
          ack_delay_time_us == basic::kUFloat16MaxValue
              ? TimeDelta::Infinite()
              : TimeDelta::FromMicroseconds(ack_delay_time_us))) {
    // The visitor suppresses further processing of the packet. Although this is
    // not a parsing error, returns false as this is in middle of processing an
    // ack frame,
    set_detailed_error("Visitor suppresses further processing of ack frame.");
    return false;
  }
  if (has_ack_blocks && !reader->ReadUInt8(&num_ack_blocks)) {
    set_detailed_error("Unable to read num of ack blocks.");
    return false;
  }

  uint64_t first_block_length;
  if (!reader->ReadBytesToUInt64(ack_block_length, &first_block_length)) {
    set_detailed_error("Unable to read first ack block length.");
    return false;
  }
  if (first_block_length == 0) {
    // For non-empty ACKs, the first block length must be non-zero.
    if (largest_acked != 0 || num_ack_blocks != 0) {
      set_detailed_error(
          QuicStrCat("First block length is zero but ACK is "
                     "not empty. largest acked is ",
                     largest_acked, ", num ack blocks is ",
                     std::to_string(num_ack_blocks), ".")
              .c_str());
      return false;
    }
  }

  if (first_block_length > largest_acked + 1) {
    set_detailed_error(QuicStrCat("Underflow with first ack block length ",
                                  first_block_length, " largest acked is ",
                                  largest_acked, ".")
                           .c_str());
    return false;
  }

  uint64_t first_received = largest_acked + 1 - first_block_length;
  if (!visitor_->OnAckRange(PacketNumber(first_received), PacketNumber(largest_acked + 1))) {
    // The visitor suppresses further processing of the packet. Although
    // this is not a parsing error, returns false as this is in middle
    // of processing an ack frame,
    set_detailed_error("Visitor suppresses further processing of ack frame.");
    return false;
  }

  if (num_ack_blocks > 0) {
    for (size_t i = 0; i < num_ack_blocks; ++i) {
      uint8_t gap = 0;
      if (!reader->ReadUInt8(&gap)) {
        set_detailed_error("Unable to read gap to next ack block.");
        return false;
      }
      uint64_t current_block_length;
      if (!reader->ReadBytesToUInt64(ack_block_length, &current_block_length)) {
        set_detailed_error("Unable to ack block length.");
        return false;
      }
      if (first_received < gap + current_block_length) {
        set_detailed_error(
            QuicStrCat("Underflow with ack block length ", current_block_length,
                       ", end of block is ", first_received - gap, ".")
                .c_str());
        return false;
      }

      first_received -= (gap + current_block_length);
      if (current_block_length > 0) {
        if (!visitor_->OnAckRange(PacketNumber(first_received),
                                  PacketNumber(first_received + current_block_length))) {
          // The visitor suppresses further processing of the packet. Although
          // this is not a parsing error, returns false as this is in middle
          // of processing an ack frame,
          set_detailed_error(
              "Visitor suppresses further processing of ack frame.");
          return false;
        }
      }
    }
  }

  if (!reader->ReadUInt8(&num_received_packets)) {
    set_detailed_error("Unable to read num received packets.");
    return false;
  }
  if(!ProcessTimestampsInAckFrame(num_received_packets,
                                   PacketNumber(largest_acked), reader)){
                                       return false;
                                   }
  // Done processing the ACK frame.
  return visitor_->OnAckFrameEnd(PacketNumber(first_received));

  return true;
}
bool ProtoFramer::ProcessTimestampsInAckFrame(uint8_t num_received_packets,
                                   PacketNumber largest_acked,
                                   basic::DataReader* reader){
  if (num_received_packets == 0) {
    return true;
  }
  uint8_t delta_from_largest_observed;
  if (!reader->ReadUInt8(&delta_from_largest_observed)) {
    set_detailed_error("Unable to read sequence delta in received packets.");
    return false;
  }
  if (largest_acked.ToUint64() <= delta_from_largest_observed) {
    set_detailed_error(QuicStrCat("delta_from_largest_observed too high: ",
                                  delta_from_largest_observed,
                                  ", largest_acked: ", largest_acked.ToUint64())
                           .c_str());
    return false;
  }

  // Time delta from the framer creation.
  uint32_t time_delta_us;
  if (!reader->ReadUInt32(&time_delta_us)) {
    set_detailed_error("Unable to read time delta in received packets.");
    return false;
  }

  PacketNumber seq_num = largest_acked - delta_from_largest_observed;
  if (process_timestamps_) {
    last_timestamp_ = CalculateTimestampFromWire(time_delta_us);

    visitor_->OnAckTimestamp(seq_num, creation_time_ + last_timestamp_);
  }

  for (uint8_t i = 1; i < num_received_packets; ++i) {
    if (!reader->ReadUInt8(&delta_from_largest_observed)) {
      set_detailed_error("Unable to read sequence delta in received packets.");
      return false;
    }
    if (largest_acked.ToUint64() <= delta_from_largest_observed) {
      set_detailed_error("delta_from_largest_observed too high");
      return false;
    }
    seq_num = largest_acked - delta_from_largest_observed;

    // Time delta from the previous timestamp.
    uint64_t incremental_time_delta_us;
    if (!reader->ReadUFloat16(&incremental_time_delta_us)) {
      set_detailed_error(
          "Unable to read incremental time delta in received packets.");
      return false;
    }

    if (process_timestamps_) {
      last_timestamp_ = last_timestamp_ + TimeDelta::FromMicroseconds(
                                              incremental_time_delta_us);
      visitor_->OnAckTimestamp(seq_num, creation_time_ + last_timestamp_);
    }
  }
  return true;
}
TimeDelta ProtoFramer::CalculateTimestampFromWire(uint32_t time_delta_us){
  // The new time_delta might have wrapped to the next epoch, or it
  // might have reverse wrapped to the previous epoch, or it might
  // remain in the same epoch. Select the time closest to the previous
  // time.
  //
  // epoch_delta is the delta between epochs. A delta is 4 bytes of
  // microseconds.
  const uint64_t epoch_delta = UINT64_C(1) << 32;
  uint64_t epoch = last_timestamp_.ToMicroseconds() & ~(epoch_delta - 1);
  // Wrapping is safe here because a wrapped value will not be ClosestTo below.
  uint64_t prev_epoch = epoch - epoch_delta;
  uint64_t next_epoch = epoch + epoch_delta;

  uint64_t time = ClosestTo(
      last_timestamp_.ToMicroseconds(), epoch + time_delta_us,
      ClosestTo(last_timestamp_.ToMicroseconds(), prev_epoch + time_delta_us,
                next_epoch + time_delta_us));

  return TimeDelta::FromMicroseconds(time);
}
bool ProtoFramer::ProcessStopWaitingFrame(basic::DataReader* reader,
                                          const ProtoPacketHeader& header,
                                          PacketNumber* least_unacked){
  uint64_t least_unacked_delta;
  if (!reader->ReadBytesToUInt64(header.packet_number_length,
                                 &least_unacked_delta)) {
    set_detailed_error("Unable to read least unacked delta.");
    return false;
  }
  if (header.packet_number.ToUint64()<= least_unacked_delta) {
    set_detailed_error("Invalid unacked delta.");
    return false;
  }
   *least_unacked=header.packet_number- least_unacked_delta;
   return true;
}
bool ProtoFramer::AppendStopWaitingFrame(const ProtoPacketHeader& header,
                              const PacketNumber& least_unacked,
                              basic::DataWriter* writer){
  DCHECK(header.packet_number >= least_unacked);
  const uint64_t least_unacked_delta =
      header.packet_number.ToUint64() - least_unacked.ToUint64();
  if (least_unacked_delta == 0) {
    return writer->WriteBytesToUInt64(header.packet_number_length,
                                      least_unacked_delta);
  }
  if (!AppendPacketNumber(header.packet_number_length,
                          PacketNumber(least_unacked_delta), writer)) {
    DLOG(FATAL)<< " seq failed: " << header.packet_number_length;
    return false;
  }
  return true;
}
}//namespace dqc;
