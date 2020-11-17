#pragma once
namespace dqc{
// These values must remain stable as they are uploaded to UMA histograms.
// To add a new error code, use the current value of QUIC_LAST_ERROR and
// increment QUIC_LAST_ERROR.
enum ProtoErrorCode{
  PROTO_NO_ERROR = 0,
  PROTO_INVALID_FRAME_DATA = 4,
    // ACK frame data is malformed.
  PROTO_INVALID_ACK_DATA = 9,
    // STREAM frame data is malformed.
  PROTO_INVALID_STREAM_DATA = 46,
    // The packet contained no payload.
  PROTO_MISSING_PAYLOAD = 48,
    // STOP_WAITING frame data is malformed.
  PROTO_INVALID_STOP_WAITING_DATA = 60,
};
const char* ProtoErrorCodeToString(ProtoErrorCode error);
}

