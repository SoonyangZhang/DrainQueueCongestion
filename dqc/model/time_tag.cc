#include "ns3/time_tag.h"
#include "ns3/core-module.h"
namespace ns3{
TypeId TimeTag::GetTypeId (void){
   static TypeId tid = TypeId ("ns3::TimeTag")
     .SetParent<Tag> ()
     .AddConstructor<TimeTag> ()
     .AddAttribute ("SimpleValue",
                    "A simple value",
                    EmptyAttributeValue (),
                    MakeUintegerAccessor (&TimeTag::GetSentTime),
                    MakeUintegerChecker<int32_t> ())
   ;
   return tid;    
}
TypeId TimeTag::GetInstanceTypeId (void) const{
    return GetTypeId ();
} 
uint32_t TimeTag::GetSerializedSize (void) const {
    return 4;
}
void TimeTag::Serialize (TagBuffer i) const{
    i.WriteU32(m_sentTime);
}
void TimeTag::Deserialize (TagBuffer i){
    m_sentTime=i.ReadU32();
}
void TimeTag::Print (std::ostream &os) const{
    os << "v=" <<m_sentTime;
}
}
