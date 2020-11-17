#include "proto_error_codes.h"
namespace dqc{
#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x;

const char* ProtoErrorCodeToString(ProtoErrorCode error) {
  switch (error) {
    RETURN_STRING_LITERAL(PROTO_NO_ERROR);
    RETURN_STRING_LITERAL(PROTO_INVALID_FRAME_DATA);
    RETURN_STRING_LITERAL(PROTO_INVALID_ACK_DATA);
    RETURN_STRING_LITERAL(PROTO_INVALID_STREAM_DATA);
    RETURN_STRING_LITERAL(PROTO_MISSING_PAYLOAD);
    RETURN_STRING_LITERAL(PROTO_INVALID_STOP_WAITING_DATA);
  }
   return "INVALID_ERROR_CODE";
}
}
