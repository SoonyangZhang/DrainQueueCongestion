#include "proto_packets.h"
namespace dqc{
bool HasStreamInRetransbleFrames(ProtoFrames &frame){
	bool ret=false;
	for(auto it=frame.begin();it!=frame.end();it++){
		if(it->type()==PROTO_FRAME_STREAM){
			ret=true;
			break;
		}
	}
	return ret;
}
}
