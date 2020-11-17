#pragma once
#include "optional.h"
namespace dqc{
class HumanoidCountType{
public:
    HumanoidCountType(int min,int max,int init):min_{min},max_{max}{
        if((init>=min_)&&(init<=max)){
            counter_=init;
        }else{
            counter_=min_;
        }

    }
    ~HumanoidCountType(){}
    int get_value(){
        return *counter_;
    }
    void reset(){
        last_=nonstd::nullopt;
        counter_=min_;
    }
    //++i;
    HumanoidCountType& operator++(){
        if(last_.has_value()){
            int v=*counter_;
            if(v>(*last_)){
                last_=v;
                if(v==max_){
                    counter_=max_-1;
                }else{
                    counter_=v+1;
                }
            }else{
                last_=v;
                if(v==min_){
                    counter_=min_+1;
                }else{
                    counter_=v-1;
                }
            }
        }else{
            last_=counter_;
            int v=*counter_;
            if(v==max_){
                counter_=v-1;
            }else{
                counter_=v+1;
            }

        }
        return *this;
    }
    //i++
    const HumanoidCountType operator++(int){
        HumanoidCountType tmp=*this;
        ++(*this);
        return HumanoidCountType(tmp);
    }
private:
    int min_;
    int max_;
    nonstd::optional<int> last_;
    nonstd::optional<int> counter_;
};
}
