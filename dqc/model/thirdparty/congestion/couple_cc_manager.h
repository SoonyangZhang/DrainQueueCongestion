#pragma once
#include "proto_send_algorithm_interface.h"
#include <vector>
namespace dqc{
class CoupleSource;
class CoupleManager{
public:
	static CoupleManager* Instance();
	void Destructor();
	void RegisterSource(CoupleSource *source);
	void OnCongestionCreate(SendAlgorithmInterface *cc);
private:
	CoupleManager(){}
	~CoupleManager();
	std::vector<CoupleSource*> sources_;
};
}
