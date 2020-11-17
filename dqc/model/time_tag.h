#pragma once
#include "ns3/tag.h"
namespace ns3{
class TimeTag:public Tag{
public:
    static TypeId GetTypeId (void);
    virtual TypeId GetInstanceTypeId (void) const override;
    virtual uint32_t GetSerializedSize (void) const override;
    virtual void Serialize (TagBuffer i) const override;
    virtual void Deserialize (TagBuffer i) override;
    virtual void Print (std::ostream &os) const override; 
    void SetSentTime(int32_t sent_ts){
        m_sentTime= sent_ts;
    }
    int32_t GetSentTime() const {
        return m_sentTime;
    }
private:
    int32_t m_sentTime{0};
};    
}
