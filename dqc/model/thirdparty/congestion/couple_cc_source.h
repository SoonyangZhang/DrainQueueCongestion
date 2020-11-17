#pragma once
#include "couple_cc_manager.h"
#include <vector>
#include <list>
namespace dqc{
class CoupleSource{
public:
	CoupleSource(){}
	~CoupleSource();
	void RegsterMonitorCongestionId(uint32_t id);
	bool NotifiCongestionCreate(SendAlgorithmInterface *cc);
private:
	bool IsIdRegistered(uint32_t id);
	void MaybeTriggerElementFull();
	void NotifyExistToOthers(SendAlgorithmInterface *cc);
	bool triggered_{false};
	std::list<SendAlgorithmInterface*> ccs_;
	std::vector<uint32_t> cids_;
};
}
