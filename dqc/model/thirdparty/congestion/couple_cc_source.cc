#include "couple_cc_source.h"
namespace dqc{
CoupleSource::~CoupleSource(){
	cids_.clear();
	ccs_.clear();
}
void CoupleSource::RegsterMonitorCongestionId(uint32_t id){
	if(!IsIdRegistered(id)){
		cids_.push_back(id);
	}
}
bool CoupleSource::NotifiCongestionCreate(SendAlgorithmInterface *cc){
	bool taken=false;
	uint32_t id=cc->GetCongestionId();
	if(IsIdRegistered(id)){
		ccs_.push_back(cc);
		MaybeTriggerElementFull();
		taken=true;
	}
	return taken;
}
bool CoupleSource::IsIdRegistered(uint32_t id){
	bool exist=false;
	for(auto it=cids_.begin();it!=cids_.end();it++){
		if(id==(*it)){
			exist=true;
			break;
		}
	}
	return exist;
}
void CoupleSource::MaybeTriggerElementFull(){
	if(triggered_){
		return;
	}
	if(ccs_.size()==cids_.size()){
		triggered_=true;
		for(auto it=ccs_.begin();it!=ccs_.end();it++){
			SendAlgorithmInterface *cc=(*it);
			NotifyExistToOthers(cc);
		}
	}
}
void CoupleSource::NotifyExistToOthers(SendAlgorithmInterface *cc){
	for(auto it=ccs_.begin();it!=ccs_.end();it++){
		if(cc!=(*it)){
			(*it)->RegisterCoupleCC(cc);
		}
	}
}
}
