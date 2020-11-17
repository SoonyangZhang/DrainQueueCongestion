#include "couple_cc_manager.h"
#include "couple_cc_source.h"
namespace dqc{
CoupleManager* CoupleManager::Instance(){
	static CoupleManager * const ins=new CoupleManager();
	return ins;
}
void CoupleManager::Destructor(){
	sources_.clear();
}
void CoupleManager::RegisterSource(CoupleSource *source){
	bool exist=false;
	for(auto it=sources_.begin();it!=sources_.end();it++){
		if(source==(*it)){
			exist=true;
			break;
		}
	}
	if(!exist){
		sources_.push_back(source);
	}
}
void CoupleManager::OnCongestionCreate(SendAlgorithmInterface *cc){
	for(auto it=sources_.begin();it!=sources_.end();it++){
		if((*it)->NotifiCongestionCreate(cc)){
			break;
		}
	}
}
}
