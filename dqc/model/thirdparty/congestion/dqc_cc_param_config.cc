#include "dqc_cc_param_config.h"
#include "flag_impl.h"
namespace dqc{
void set_reno_beta(float value){
    SetQuicReloadableFlag(reno_beta,value);
}
float get_reno_beta(){
    return GetQuicReloadableFlag(reno_beta);
}
}
