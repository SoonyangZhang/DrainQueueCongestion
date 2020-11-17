#include <memory.h>
#include <limits>
#include "byte_codec.h"
#include "basic_constants.h"
#include "logging.h"
namespace basic{
// Maximum value that can be properly encoded using VarInt62 coding.
const uint64_t kVarInt62MaxValue = UINT64_C(0x3fffffffffffffff);

// VarInt62 encoding masks
// If a uint64_t anded with a mask is not 0 then the value is encoded
// using that length (or is too big, in the case of kVarInt62ErrorMask).
// Values must be checked in order (error, 8-, 4-, and then 2- bytes)
// and if none are non-0, the value is encoded in 1 byte.
const uint64_t kVarInt62ErrorMask = UINT64_C(0xc000000000000000);
const uint64_t kVarInt62Mask8Bytes = UINT64_C(0x3fffffffc0000000);
const uint64_t kVarInt62Mask4Bytes = UINT64_C(0x000000003fffc000);
const uint64_t kVarInt62Mask2Bytes = UINT64_C(0x0000000000003fc0);
enum VariableLengthIntegerLength : uint8_t {
  // Length zero means the variable length integer is not present.
  VARIABLE_LENGTH_INTEGER_LENGTH_0 = 0,
  VARIABLE_LENGTH_INTEGER_LENGTH_1 = 1,
  VARIABLE_LENGTH_INTEGER_LENGTH_2 = 2,
  VARIABLE_LENGTH_INTEGER_LENGTH_4 = 4,
  VARIABLE_LENGTH_INTEGER_LENGTH_8 = 8,

  // By default we write the IETF long header length using the 2-byte encoding
  // of variable length integers, even when the length is below 64, which allows
  // us to fill in the length before knowing what the length actually is.
  kBasicDefaultLongHeaderLengthLength = VARIABLE_LENGTH_INTEGER_LENGTH_2,
};
uint8_t GetVarInt62Len(uint64_t value){
  if ((value & kVarInt62ErrorMask) != 0) {
    CHECK(0);
    return VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  if ((value & kVarInt62Mask8Bytes) != 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_8;
  }
  if ((value & kVarInt62Mask4Bytes) != 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_4;
  }
  if ((value & kVarInt62Mask2Bytes) != 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
  return VARIABLE_LENGTH_INTEGER_LENGTH_1;    
}
DataReader::DataReader(const char* buf,uint32_t len)
:DataReader(buf,len,NETWORK_ORDER){}
DataReader::DataReader(const char* buf,uint32_t len,Endianness endianness)
:data_(buf)
,len_(len)
,pos_(0)
,endianness_(endianness){
}
bool DataReader::ReadUInt8(uint8_t *result){
    return ReadBytes(result, sizeof(uint8_t));
}
bool DataReader::ReadUInt16(uint16_t *result){
    if(!ReadBytes(result,sizeof(uint16_t))){
        return false;
    }
    if(endianness_ == NETWORK_ORDER){
        *result=basic::NetToHost16(*result);
    }
    return true;
}
bool DataReader::ReadUInt32(uint32_t *result){
    if(!ReadBytes(result,sizeof(uint32_t))){
        return false;
    }
    if(endianness_ == NETWORK_ORDER){
        *result=basic::NetToHost32(*result);
    }
    return true;
}
bool DataReader::ReadUInt64(uint64_t *result){
    if(!ReadBytes(result,sizeof(uint64_t))){
        return false;
    }
    if(endianness_ == NETWORK_ORDER){
        *result=basic::NetToHost64(*result);
    }
    return true;
}
bool DataReader::ReadBytesToUInt64(uint32_t num_len,uint64_t *result){
    *result=0u;
    if(HOST_ORDER==endianness_){
        return ReadBytes(result,num_len);
    }
    if(!ReadBytes(reinterpret_cast<char*>(result) + sizeof(*result) - num_len,num_len)){
        return false;
    }
    *result=basic::NetToHost64(*result);
    return true;
}
bool DataReader::ReadBytes(void*result,uint32_t size){
    if(!CanRead(size)){
        OnFailure();
        return false;
    }
    memcpy(result,data_+pos_,size);
    pos_+=size;
    return true;
}
bool DataReader::ReadUFloat16(uint64_t* result){
  uint16_t value;
  if (!ReadUInt16(&value)) {
    return false;
  }

  *result = value;
  if (*result < (1 << kUFloat16MantissaEffectiveBits)) {
    // Fast path: either the value is denormalized (no hidden bit), or
    // normalized (hidden bit set, exponent offset by one) with exponent zero.
    // Zero exponent offset by one sets the bit exactly where the hidden bit is.
    // So in both cases the value encodes itself.
    return true;
  }

  uint16_t exponent =
      value >> kUFloat16MantissaBits;  // No sign extend on uint!
  // After the fast pass, the exponent is at least one (offset by one).
  // Un-offset the exponent.
  --exponent;
  DCHECK_GE(exponent, 1);
  DCHECK_LE(exponent, kUFloat16MaxExponent);
  // Here we need to clear the exponent and set the hidden bit. We have already
  // decremented the exponent, so when we subtract it, it leaves behind the
  // hidden bit.
  *result -= exponent << kUFloat16MantissaBits;
  *result <<= exponent;
  DCHECK_GE(*result,
            static_cast<uint64_t>(1 << kUFloat16MantissaEffectiveBits));
  DCHECK_LE(*result, kUFloat16MaxValue);
  return true;
}
bool DataReader::ReadStringPiece16(std::string * result){
    uint16_t result_len=0;
    if(!ReadUInt16(&result_len)){
        return false;
    }
    return ReadStringPiece(result,result_len);
}
bool DataReader::ReadStringPiece(std::string * result, size_t size){
      // Make sure that we have enough data to read.
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }
  *result=std::string(data_ + pos_, size);
  // Iterate.
  pos_ += size;
    return true;
}
bool DataReader::IsDoneReading() const {
  return len_ == pos_;
}
size_t DataReader::BytesRemaining() const {
  return len_ - pos_;
}
uint8_t DataReader::PeekVarInt62Length() {
  DCHECK_EQ(endianness(), NETWORK_ORDER);
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data() + pos());
  if (BytesRemaining() == 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  return static_cast<uint8_t>(
      1 << ((*next & 0b11000000) >> 6));
}
bool DataReader::ReadVarInt62(uint64_t* result){
  DCHECK_EQ(endianness(),NETWORK_ORDER);

  size_t remaining = BytesRemaining();
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data() + pos());
  if (remaining != 0) {
    switch (*next & 0xc0) {
      case 0xc0:
        // Leading 0b11...... is 8 byte encoding
        if (remaining >= 8) {
          *result = (static_cast<uint64_t>((*(next)) & 0x3f) << 56) +
                    (static_cast<uint64_t>(*(next + 1)) << 48) +
                    (static_cast<uint64_t>(*(next + 2)) << 40) +
                    (static_cast<uint64_t>(*(next + 3)) << 32) +
                    (static_cast<uint64_t>(*(next + 4)) << 24) +
                    (static_cast<uint64_t>(*(next + 5)) << 16) +
                    (static_cast<uint64_t>(*(next + 6)) << 8) +
                    (static_cast<uint64_t>(*(next + 7)) << 0);
          AdvancePos(8);
          return true;
        }
        return false;

      case 0x80:
        // Leading 0b10...... is 4 byte encoding
        if (remaining >= 4) {
          *result = (((*(next)) & 0x3f) << 24) + (((*(next + 1)) << 16)) +
                    (((*(next + 2)) << 8)) + (((*(next + 3)) << 0));
          AdvancePos(4);
          return true;
        }
        return false;

      case 0x40:
        // Leading 0b01...... is 2 byte encoding
        if (remaining >= 2) {
          *result = (((*(next)) & 0x3f) << 8) + (*(next + 1));
          AdvancePos(2);
          return true;
        }
        return false;

      case 0x00:
        // Leading 0b00...... is 1 byte encoding
        *result = (*next) & 0x3f;
        AdvancePos(1);
        return true;
    }
  }
  return false;    
}
void DataReader::AdvancePos(size_t amount) {
  DCHECK_LE(pos_, std::numeric_limits<size_t>::max() - amount);
  DCHECK_LE(pos_, len_ - amount);
  pos_ += amount;
}
bool DataReader::CanRead(uint32_t bytes){
    return bytes<=(len_-pos_);
}
void DataReader::OnFailure(){
    pos_=len_;
}
DataWriter::DataWriter(char* buf,uint32_t len)
:DataWriter(buf,len,NETWORK_ORDER){
}
DataWriter::DataWriter(char* buf,uint32_t len,Endianness endianness)
:buf_(buf)
,pos_(0)
,capacity_(len)
,endianness_(endianness){
}
bool DataWriter::WriteUInt8(uint8_t value){
    return WriteBytes(&value,sizeof(uint8_t));
}
bool DataWriter::WriteUInt16(uint16_t value){
    if(endianness_ == NETWORK_ORDER){
        value=basic::HostToNet16(value);
    }
    return WriteBytes(&value,sizeof(uint16_t));
}
bool DataWriter::WriteUInt32(uint32_t value){
    if(endianness_ == NETWORK_ORDER){
        value=basic::HostToNet32(value);
    }
    return WriteBytes(&value,sizeof(uint32_t));
}
bool DataWriter::WriteUInt64(uint64_t value){
    if(endianness_ == NETWORK_ORDER){
        value=basic::HostToNet64(value);
    }
    return WriteBytes(&value,sizeof(uint64_t));
}
bool DataWriter::WriteBytesToUInt64(uint32_t num_bytes, uint64_t value){
    if(num_bytes>sizeof(value)){
        return false;
    }
    if(HOST_ORDER==endianness_){
        return WriteBytes(&value,num_bytes);
    }
    value=basic::HostToNet64(value);
    return WriteBytes(reinterpret_cast<char*>(&value)+sizeof(value)-num_bytes,num_bytes);
}
bool DataWriter::WriteBytes(const void *value,uint32_t size){
    char *dst=BeginWrite(size);
    if(!dst){
        return false;
    }
    memcpy((void*)dst,value,size);
    pos_+=size;
    return true;
}
bool DataWriter::WriteUFloat16(uint64_t value){
  uint16_t result;
  if (value < (UINT64_C(1) << kUFloat16MantissaEffectiveBits)) {
    // Fast path: either the value is denormalized, or has exponent zero.
    // Both cases are represented by the value itself.
    result = static_cast<uint16_t>(value);
  } else if (value >= kUFloat16MaxValue) {
    // Value is out of range; clamp it to the maximum representable.
    result = std::numeric_limits<uint16_t>::max();
  } else {
    // The highest bit is between position 13 and 42 (zero-based), which
    // corresponds to exponent 1-30. In the output, mantissa is from 0 to 10,
    // hidden bit is 11 and exponent is 11 to 15. Shift the highest bit to 11
    // and count the shifts.
    uint16_t exponent = 0;
    for (uint16_t offset = 16; offset > 0; offset /= 2) {
      // Right-shift the value until the highest bit is in position 11.
      // For offset of 16, 8, 4, 2 and 1 (binary search over 1-30),
      // shift if the bit is at or above 11 + offset.
      if (value >= (UINT64_C(1) << (kUFloat16MantissaBits + offset))) {
        exponent += offset;
        value >>= offset;
      }
    }

    DCHECK_GE(exponent, 1);
    DCHECK_LE(exponent, kUFloat16MaxExponent);
    DCHECK_GE(value, UINT64_C(1) << kUFloat16MantissaBits);
    DCHECK_LT(value, UINT64_C(1) << kUFloat16MantissaEffectiveBits);

    // Hidden bit (position 11) is set. We should remove it and increment the
    // exponent. Equivalently, we just add it to the exponent.
    // This hides the bit.
    result = static_cast<uint16_t>(value + (exponent << kUFloat16MantissaBits));
  }

  if (endianness_ == NETWORK_ORDER) {
    result = basic::HostToNet16(result);
  }
  return WriteBytes(&result, sizeof(result));
}
bool DataWriter::WriteVarInt62(uint64_t value){
  DCHECK_EQ(endianness(), NETWORK_ORDER);

  size_t remaining_bytes = remaining();
  char* next = buffer() + length();

  if ((value & kVarInt62ErrorMask) == 0) {
    // We know the high 2 bits are 0 so |value| is legal.
    // We can do the encoding.
    if ((value & kVarInt62Mask8Bytes) != 0) {
      // Someplace in the high-4 bytes is a 1-bit. Do an 8-byte
      // encoding.
      if (remaining_bytes >= 8) {
        *(next + 0) = ((value >> 56) & 0x3f) + 0xc0;
        *(next + 1) = (value >> 48) & 0xff;
        *(next + 2) = (value >> 40) & 0xff;
        *(next + 3) = (value >> 32) & 0xff;
        *(next + 4) = (value >> 24) & 0xff;
        *(next + 5) = (value >> 16) & 0xff;
        *(next + 6) = (value >> 8) & 0xff;
        *(next + 7) = value & 0xff;
        IncreaseLength(8);
        return true;
      }
      return false;
    }
    // The high-order-4 bytes are all 0, check for a 1, 2, or 4-byte
    // encoding
    if ((value & kVarInt62Mask4Bytes) != 0) {
      // The encoding will not fit into 2 bytes, Do a 4-byte
      // encoding.
      if (remaining_bytes >= 4) {
        *(next + 0) = ((value >> 24) & 0x3f) + 0x80;
        *(next + 1) = (value >> 16) & 0xff;
        *(next + 2) = (value >> 8) & 0xff;
        *(next + 3) = value & 0xff;
        IncreaseLength(4);
        return true;
      }
      return false;
    }
    // The high-order bits are all 0. Check to see if the number
    // can be encoded as one or two bytes. One byte encoding has
    // only 6 significant bits (bits 0xffffffff ffffffc0 are all 0).
    // Two byte encoding has more than 6, but 14 or less significant
    // bits (bits 0xffffffff ffffc000 are 0 and 0x00000000 00003fc0
    // are not 0)
    if ((value & kVarInt62Mask2Bytes) != 0) {
      // Do 2-byte encoding
      if (remaining_bytes >= 2) {
        *(next + 0) = ((value >> 8) & 0x3f) + 0x40;
        *(next + 1) = (value)&0xff;
        IncreaseLength(2);
        return true;
      }
      return false;
    }
    if (remaining_bytes >= 1) {
      // Do 1-byte encoding
      *next = (value & 0x3f);
      IncreaseLength(1);
      return true;
    }
    return false;
  }
  // Can not encode, high 2 bits not 0
  return false;
}
void DataWriter::IncreaseLength(size_t delta){
    DCHECK_LE(pos_, std::numeric_limits<size_t>::max() - delta);
    DCHECK_LE(pos_, capacity_ - delta);
    pos_ += delta;
}
char* DataWriter::BeginWrite(uint32_t bytes){
    if(pos_>capacity_){
        return nullptr;
    }
    if(capacity_-pos_<bytes){
        return nullptr;
    }
    return buf_+pos_;
}
}
