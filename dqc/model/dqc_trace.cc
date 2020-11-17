#include <unistd.h>
#include <memory.h>
#include "ns3/dqc_trace.h"
#include "ns3/simulator.h"
//app http://www.cplusplus.com/reference/fstream/fstream/open/
namespace ns3{
DqcTrace::DqcTrace(int id):m_id(id){}
DqcTrace::~DqcTrace(){
    Close();
}
void DqcTrace::Log(std::string name,uint8_t enable){
	if(enable&E_DQC_OWD){
		OpenOwdFile(name);
	}
	if(enable&E_DQC_RTT){
		OpenRttFile(name);
	}
	if(enable&E_DQC_BW){
		OpenBandwidthFile(name);
	}
	if(enable&E_DQC_GOODPUT){
		OpenGoodputFile(name);
	}
    if(enable&E_DQC_STAT){
        OpenStatsFile(name);
    }
}
void DqcTrace::OpenOwdFile(std::string name){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+name+"_owd.txt";
	m_owd.open(path.c_str(), std::fstream::out);    
}
void DqcTrace::OpenRttFile(std::string name){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+name+"_rtt.txt";
	m_rtt.open(path.c_str(), std::fstream::out);    
}
void DqcTrace::OpenBandwidthFile(std::string name){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+name+"_bw.txt";
	m_bw.open(path.c_str(), std::fstream::out);     
}
void DqcTrace::OpenGoodputFile(std::string name){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+name+"_good.txt";
	m_googput.open(path.c_str(), std::fstream::out);  	
}
void DqcTrace::OpenStatsFile(std::string name){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+name+"_stats.txt";
	m_stats.open(path.c_str(), std::fstream::out);    
}
void DqcTrace::OnOwd(uint32_t seq,uint32_t owd,uint32_t size){
	if(m_owd.is_open()){
        char line [256];
        memset(line,0,256);
		float now=Simulator::Now().GetSeconds();
        sprintf (line, "%f %16d %16d %16d",
                now,seq,owd,size);
		m_owd<<line<<std::endl;
	}    
}
void DqcTrace::OnRtt(uint32_t seq,uint32_t rtt){
	if(m_rtt.is_open()){
		float now=Simulator::Now().GetSeconds();
		m_rtt<<now<<"\t"<<seq<<"\t"<<rtt<<std::endl;
	}    
}
void DqcTrace::OnBw(int32_t kbps){
    if(m_bw.is_open()){
        char line [256];
        memset(line,0,256);
        float now=Simulator::Now().GetSeconds();
        sprintf (line, "%f %16d",
                now,kbps);
        m_bw<<line<<std::endl;
    }       
    
}
void DqcTrace::OnGoodput(uint32_t kbps){
	if(m_googput.is_open()){
		float now=Simulator::Now().GetSeconds();
        m_googput<<now<<"\t"<<kbps<<std::endl;
	}     	
}
void DqcTrace::OnStats(uint64_t recv_count,uint64_t largest,
                        uint64_t recv_bytes,uint64_t duration,
                       float avg_owd){
	if(m_stats.is_open()){
        double loss_rate=10000.0-10000.0*recv_count/largest;
        m_stats<<(float)(loss_rate/100)<<std::endl;
        uint32_t avg_kbps=recv_bytes*8/duration;
        m_stats<<avg_kbps<<std::endl;
        m_stats<<avg_owd<<std::endl;
        m_stats<<recv_bytes<<std::endl;
        m_stats.flush();
    }
    if(!m_traceStatsCb.IsNull()){
        m_traceStatsCb(m_id,recv_count,largest,recv_bytes,avg_owd);
    }
}
void DqcTrace::Close(){
    CloseOwdFile();
    CloseRttFile();
    CloseBandwidthFile();
	CloseGoodputFile();
    CloseStatsFile();
}
void DqcTrace::CloseOwdFile(){
	if(m_owd.is_open()){
		m_owd.close();
	}    
}
void DqcTrace::CloseRttFile(){
	if(m_rtt.is_open()){
		m_rtt.close();
	}    
}
void DqcTrace::CloseBandwidthFile(){
    if(m_bw.is_open()){
        m_bw.close();
    }
} 
void DqcTrace::CloseGoodputFile(){
    if(m_googput.is_open()){
        m_googput.flush();
        m_googput.close();
    }	
}
void DqcTrace::CloseStatsFile(){
    if(m_stats.is_open()){
        m_stats.close();
    }
}
DqcTraceState::DqcTraceState(std::string name){
	char buf[FILENAME_MAX];
	memset(buf,0,FILENAME_MAX);
	std::string path = std::string (getcwd(buf, FILENAME_MAX)) + "/traces/"
			+name+"_all_stats.txt";
	m_stats.open(path.c_str(), std::fstream::out);     
}
DqcTraceState::~DqcTraceState(){
    if(m_stats.is_open()){
        m_stats.close();
    }
}
void DqcTraceState::OnStats(uint32_t id,uint64_t recv_count,uint64_t largest,
                 uint64_t recv_bytes,float avg_owd){
    m_recvCount+=recv_count;
    m_totalRecv+=largest;
    m_totalRecvBytes+=recv_bytes;
    if(!m_ccType1Ids.empty()){
        auto it=m_ccType1Ids.find(id);
        if(it!=m_ccType1Ids.end()){
            m_ccType1TotalRecvBytes+=recv_bytes;
        }else{
            m_ccType2TotalRecvBytes+=recv_bytes;
        }
    }
    if(m_delayIds.empty()){
        return;
    }
    auto it=m_delayIds.find(id);
    if(it!=m_delayIds.end()){
        m_delayCount+=recv_count;
        m_sumDelay+=(avg_owd*recv_count);
    }
}
void DqcTraceState::Flush(uint32_t capacity,uint32_t simulation_time){
    m_count++;
    double average_rate=1.0*m_totalRecvBytes*8/simulation_time;
    double util=(average_rate/capacity);
    double loss=10000.0-10000.0*m_recvCount/m_totalRecv;
    float loss_rate=loss/100;
    double delay=0.0;
    double ratio=0.0;
    if(m_ccType2TotalRecvBytes>0){
        ratio=1.0*m_ccType1TotalRecvBytes/m_ccType2TotalRecvBytes;
    }
    if(!m_delayIds.empty()&&m_delayCount>0){
        delay=1.0*m_sumDelay/m_delayCount;
    }
    if(m_stats.is_open()){
        char line [256];
        memset(line,0,256);
        sprintf (line, "%d %16f %16f %16f %16f %16f",
                m_count,(float)loss_rate,(float)average_rate,
                (float)delay,(float)util,(float)ratio);
        m_stats<<line<<std::endl;        
    }
    Reset();
}
void DqcTraceState::RecordRuningTime(float millis,float mimutes){
    if(m_stats.is_open()){
        char line [256];
        memset(line,0,256);
        sprintf (line, "%f %16f",millis,mimutes);
        m_stats<<line<<std::endl;
    }
}
void DqcTraceState::ReisterAvgDelayId(uint32_t id){
    m_delayIds.insert(id);
}
void DqcTraceState::RegisterCongestionType(uint32_t id,uint32_t type){
    if(!type){
        m_ccType1Ids.insert(id);
    }
}
void DqcTraceState::Reset(){
    m_recvCount=0;
    m_totalRecv=0;
    m_totalRecvBytes=0;
    uint64_t m_delayCount=0;
    uint64_t m_sumDelay=0;
    m_delayIds.clear();
}
}
