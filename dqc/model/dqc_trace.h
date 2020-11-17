#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include "ns3/callback.h"
namespace ns3{
enum DqcTraceEnable:uint8_t{
	E_DQC_OWD=0x01,
	E_DQC_RTT=0x02,
	E_DQC_BW=0x04,
    E_DQC_GOODPUT=0x08,
    E_DQC_STAT=0x010,
	E_DQC_ALL=E_DQC_OWD|E_DQC_RTT|E_DQC_BW|E_DQC_STAT,
};
class DqcTrace{
public:
	DqcTrace(int id=0);
	~DqcTrace();
	typedef Callback<void,uint32_t,uint64_t,uint64_t,uint64_t,float> TraceStats;
	void SetStatsTraceFuc(TraceStats cb){
		m_traceStatsCb=cb;
	}
	void Log(std::string name,uint8_t enable);
	void OnOwd(uint32_t seq,uint32_t owd,uint32_t size);
    void OnRtt(uint32_t seq,uint32_t rtt);
	void OnBw(int32_t kbps);
    void OnGoodput(uint32_t kbps);
    void OnStats(uint64_t recv_count,uint64_t largest,
                 uint64_t recv_bytes,uint64_t duration,
                       float avg_owd);
private:
	void Close();
	void OpenOwdFile(std::string name);
    void OpenRttFile(std::string name);
    void OpenBandwidthFile(std::string name);
    void OpenGoodputFile(std::string name);
    void OpenStatsFile(std::string name);
    void CloseOwdFile();
    void CloseRttFile();
    void CloseBandwidthFile();
    void CloseGoodputFile();
    void CloseStatsFile();
    int m_id=0;
    TraceStats m_traceStatsCb;
	std::fstream m_owd;
	std::fstream m_rtt;
    std::fstream m_bw;
    std::fstream m_googput;
    std::fstream m_stats;
};
class DqcTraceState{
public:
    DqcTraceState(std::string name);
    ~DqcTraceState();
    void OnStats(uint32_t id,uint64_t recv_count,uint64_t largest,
                 uint64_t recv_bytes,float avg_owd);
    void Flush(uint32_t capacity,uint32_t simulation_time);
    void RecordRuningTime(float millis,float mimutes);
    void ReisterAvgDelayId(uint32_t id);
    //compare inter -protocol fairness and rtt unfairness
    void RegisterCongestionType(uint32_t id,uint32_t type=0);
    void Reset();
private:
    std::fstream m_stats;
    int m_count=0;
    uint64_t m_recvCount=0;
    uint64_t m_totalRecv=0;
    uint64_t m_totalRecvBytes=0;
    uint64_t m_delayCount=0;
    uint64_t m_sumDelay=0;
    std::set<uint32_t> m_delayIds;
    std::set<uint32_t> m_ccType1Ids;
    uint64_t m_ccType1TotalRecvBytes=0;
    uint64_t m_ccType2TotalRecvBytes=0;
};
}
