#include "flag_config.h"
#include "flag_impl.h"
#include "flag_util_impl.h"
namespace dqc{
    void SetRenoBeta(float beta){
        SetQuicReloadableFlag(reno_beta,beta);
    }
}
