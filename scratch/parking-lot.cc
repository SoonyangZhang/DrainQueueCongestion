// running on ns3.31
/** Network topology
 *       n0            n1             n2          n3
 *        |            |              |            |
 *        | l0         | l2           |l4          |l6
 *        |            |              |            |
 *        n4---l1------n5-----l3-----n6-----l5-----n7
 *        |            |              |            |
 *        |  l7        |  l8          |l9          |l10
 *        |            |              |            |
 *        n8           n9             n10         n11
 */
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/dqc-module.h"
#include <stdint.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <memory>
#include <set>
#include <chrono>
using namespace ns3;
using namespace dqc;
using namespace std;
enum LinkAqmType :uint8_t{
AQM_DROP,
AQM_RED,
AQM_CODEL,
};
struct LinkProperty{
LinkAqmType aqm=AQM_DROP;
uint16_t src;
uint16_t dst;
uint64_t bps;
uint32_t msDelay;
uint32_t msQdelay;
uint32_t rttDelay;
uint32_t loss_integer=0;
};
struct LinkToNodes{
    uint32_t index;
    uint16_t src;
    uint16_t dst;    
};
static int ip=1;
static uint32_t codelTargetDelay=50;//in ms
static NodeContainer BuildTopology(uint16_t hosts,int links,
                                    LinkProperty *property,int bottleneck,uint32_t mtu,uint32_t red_delay=100){
    NodeContainer topo;
    topo.Create (hosts);
    InternetStackHelper stack;
    stack.Install (topo);
    for(int i=0;i<links;i++){
        NodeContainer nodes=NodeContainer (topo.Get (property[i].src), topo.Get (property[i].dst));
        uint64_t bps=property[i].bps;
        uint32_t msQdelay=property[i].msQdelay;
        uint32_t msDelay=property[i].msDelay;
        PointToPointHelper pointToPoint;
        pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
        pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (msDelay)));
        if(bottleneck==i){
            pointToPoint.SetQueue ("ns3::DropTailQueue",
                           "MaxSize", StringValue (std::to_string(10)+"p"));            
        }else{
            auto bufSize = std::max<uint32_t> (mtu, bps * msQdelay / 8000);
            int packets=bufSize/mtu;
            pointToPoint.SetQueue ("ns3::DropTailQueue",
                           "MaxSize", StringValue (std::to_string(packets)+"p"));              
        }
        NetDeviceContainer devices = pointToPoint.Install (nodes);
        if(bottleneck==i){
            if(property[i].aqm==AQM_RED){
                auto bdp = std::max<uint32_t> (mtu, bps * red_delay / 8000);
                int packets=bdp/mtu;
                double max_th=1.5*packets;
                double min_th=1.0*packets;
                TrafficControlHelper tchRed;
                tchRed.SetRootQueueDisc ("ns3::RedQueueDisc",
                                        "LinkBandwidth", DataRateValue(DataRate(bps)),
                                        "LinkDelay", TimeValue (MilliSeconds (msDelay)),
                                        "MeanPktSize",UintegerValue(mtu),
                                        "MinTh", DoubleValue (min_th),
                                        "MaxTh", DoubleValue (max_th),
                                        "MaxSize",QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS,packets*2)),
                                        "UseEcn",BooleanValue (true),
                                        "MeanPktSize",UintegerValue(mtu));
                tchRed.Install(devices);
            }else if(property[i].aqm==AQM_CODEL){
                auto bufSize = std::max<uint32_t> (mtu, bps * msQdelay / 8000);
                int packets=bufSize/mtu;
                TrafficControlHelper tchCoDel;
                tchCoDel.SetRootQueueDisc ("ns3::CoDelQueueDisc",
                            "MaxSize",QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS,packets)),
                                "UseEcn",BooleanValue (true),
                                "Target",TimeValue (MilliSeconds (codelTargetDelay)));
                tchCoDel.Install(devices);            
            }else{
                auto bufSize = std::max<uint32_t> (mtu, bps * msQdelay / 8000);
                int packets=bufSize/mtu;
                TrafficControlHelper pfifoHelper;
                uint16_t handle = pfifoHelper.SetRootQueueDisc ("ns3::FifoQueueDisc", "MaxSize", StringValue (std::to_string(packets)+"p"));
                pfifoHelper.AddInternalQueues (handle, 1, "ns3::DropTailQueue", "MaxSize",StringValue (std::to_string(packets)+"p"));
                pfifoHelper.Install(devices);            
            }            
        }
        Ipv4AddressHelper address;
        std::string nodeip="10.1."+std::to_string(ip)+".0";
        ip++;
        address.SetBase (nodeip.c_str(), "255.255.255.0");
        address.Assign (devices);
        if(property[i].loss_integer>0){
            double loss_rate=property[i].loss_integer*1.0/1000;
            std::string errorModelType = "ns3::RateErrorModel";
            ObjectFactory factory;
            factory.SetTypeId (errorModelType);
            Ptr<ErrorModel> em = factory.Create<ErrorModel> ();
            em->SetAttribute ("ErrorRate", DoubleValue (loss_rate));
            em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
            devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));	            
        }
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    return topo;
}
void SetLinkProperty(LinkProperty *property,uint16_t src,uint16_t dst,
uint64_t bps,uint32_t msDelay,uint32_t msQdelay,uint32_t rttDelay=0){
    property->src=src;
    property->dst=dst;
    property->bps=bps;
    property->msDelay=msDelay;
    property->msQdelay=msQdelay;
    property->rttDelay=rttDelay;
    property->loss_integer=0;
    if(rttDelay==0){
       property->rttDelay=msQdelay;
    }    
}
static void InstallDqc( dqc::CongestionControlType cc_type,
                        Ptr<Node> sender,Ptr<Node> receiver,
                        uint16_t send_port,uint16_t recv_port,
                        float startTime,float stopTime,
                        DqcTrace *trace, DqcTraceState *stat,
                        uint32_t max_bps=0,uint32_t cid=0,bool ecn=false,uint32_t emucons=1)
{
    Ptr<DqcSender> sendApp = CreateObject<DqcSender> (cc_type,ecn);
    Ptr<DqcReceiver> recvApp = CreateObject<DqcReceiver>();
    sender->AddApplication (sendApp);
    receiver->AddApplication (recvApp);
    sendApp->SetNumEmulatedConnections(emucons);
    Ptr<Ipv4> ipv4 = receiver->GetObject<Ipv4> ();
    Ipv4Address receiverIp = ipv4->GetAddress (1, 0).GetLocal ();
    recvApp->Bind(recv_port);
    sendApp->Bind(send_port);
    sendApp->ConfigurePeer(receiverIp,recv_port);
    sendApp->SetStartTime (Seconds (startTime));
    sendApp->SetStopTime (Seconds (stopTime));
    recvApp->SetStartTime (Seconds (startTime));
    recvApp->SetStopTime (Seconds (stopTime));
    if(max_bps>0){
        sendApp->SetMaxBandwidth(max_bps);
    }
    if(cid){
       sendApp->SetSenderId(cid);
        sendApp->SetCongestionId(cid);
    }
    if(trace){
        //sendApp->SetBwTraceFuc(MakeCallback(&DqcTrace::OnBw,trace));
        //recvApp->SetOwdTraceFuc(MakeCallback(&DqcTrace::OnOwd,trace));
        recvApp->SetGoodputTraceFuc(MakeCallback(&DqcTrace::OnGoodput,trace));
        recvApp->SetStatsTraceFuc(MakeCallback(&DqcTrace::OnStats,trace));
        trace->SetStatsTraceFuc(MakeCallback(&DqcTraceState::OnStats,stat));
    }
}
LinkToNodes linkTable[11]={
    [0]={0,0,4},
    [1]={1,4,5},
    [2]={2,5,1},
    [3]={3,5,6},
    [4]={4,6,2},
    [5]={5,6,7},
    [6]={6,7,3},
    [7]={7,4,8},
    [8]={8,5,9},
    [9]={9,6,10},
    [10]={10,7,11},        
};
LinkProperty linkProperty[11];
void same_fair_topo(int ins,std::string aqm,std::string algo,DqcTraceState *stat,int sim_time=200,int loss_integer=0,bool syn=false){
    std::string instance=std::to_string(ins);
    int links=sizeof(linkTable)/sizeof(linkTable[0]);
    uint16_t hosts=0;
    std::set<int> hostsTable;
    for(int i=0;i<links;i++){
        hostsTable.insert(linkTable[i].src);
        hostsTable.insert(linkTable[i].dst);
    }
    double sim_dur=sim_time;
    int start_time=0;
    int end_time=sim_time;
    int flows=6;
    int gap=0;
    if(!syn){
        gap=(end_time-start_time)/(2*flows-1);
        if(gap<=0){return ;}        
    }
    hosts=hostsTable.size();
    int bottleIndex=1;
    uint32_t capacity=12000000;
    uint32_t bottleBw=capacity;
    uint32_t bottleDelay=5;
    uint32_t bottleQdelay=60;
    float reno_beta=0.5;
    uint32_t rtt=30; 
    uint32_t red_delay=rtt;
    if(instance==std::string("1")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=60;
        rtt=bottleQdelay;
        red_delay=bottleQdelay;
        codelTargetDelay=40; 
    }else if(instance==std::string("2")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=90;
        rtt=60;
        red_delay=bottleQdelay;
        codelTargetDelay=60;         
    }else if(instance==std::string("3")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=120; 
        rtt=60;
        red_delay=bottleQdelay;
        codelTargetDelay=80;        
    }else if(instance==std::string("4")){
        bottleBw=capacity;
        bottleDelay=15;
        bottleQdelay=90;
        rtt=90;
        red_delay=bottleQdelay;
        codelTargetDelay=60;  
    }else if(instance==std::string("5")){
        bottleBw=capacity;
        bottleDelay=15;
        bottleQdelay=135; 
        rtt=90;
        red_delay=bottleQdelay;
        codelTargetDelay=90;         
    }else if(instance==std::string("6")){
        bottleBw=capacity;
        bottleDelay=15;
        bottleQdelay=180;
        rtt=90;
        red_delay=bottleQdelay;
        codelTargetDelay=120;          
    }else if(instance==std::string("7")){
        bottleBw=capacity;
        bottleDelay=20;
        bottleQdelay=120;
        rtt=120;
        red_delay=bottleQdelay;
        codelTargetDelay=80;  
    }else if(instance==std::string("8")){
        bottleBw=capacity;
        bottleDelay=20;
        bottleQdelay=180;
        rtt=120;
        red_delay=bottleQdelay; 
        codelTargetDelay=120;         
    }else if(instance==std::string("9")){
        bottleBw=capacity;
        bottleDelay=20;
        bottleQdelay=240;
        rtt=120;
        red_delay=bottleQdelay;
        codelTargetDelay=160;          
    }   
    uint32_t bps=4*bottleBw;
    uint32_t delay=bottleDelay;
    uint32_t qDelay=4*bottleQdelay;
   
    LinkAqmType bottleAqm=AQM_DROP;
    
    if(aqm.compare("codel")==0){
        bottleAqm=AQM_CODEL;       
    }else if(aqm.compare("red")==0){
        bottleAqm=AQM_RED;
    }else{
        aqm=std::string("drop");
    }
    for(int i=0;i<links;i++){
        uint16_t src=linkTable[i].src;
        uint16_t dst=linkTable[i].dst;
        SetLinkProperty(&linkProperty[i],src,dst,bps,delay,qDelay,rtt);
        if(bottleIndex==i){
            linkProperty[i].bps=bottleBw;
            linkProperty[i].msDelay=bottleDelay;
            linkProperty[i].msQdelay=bottleQdelay;
            linkProperty[i].aqm=bottleAqm;
            linkProperty[i].loss_integer=loss_integer;
        }
    }
    uint32_t mtu=1500;
    NodeContainer topo=BuildTopology(hosts,links,linkProperty,bottleIndex,mtu,red_delay);
    
    dqc::CongestionControlType cc=kRenoBytes;
    bool ecn=false;
    if(algo.compare("bbr")==0){
        cc=kBBR;
    }else if(algo.compare("bbrd")==0){
        cc=kBBRD;
    }else if(algo.compare("quicbbr")==0){
        cc=kQuicBBR;
    }else if(algo.compare("quicbbrd")==0){
        cc=kQuicBBRD;
    }else if(algo.compare("quicbbr2")==0){
        cc=kBBRv2;
    }else if(algo.compare("quicbbr2ecn")==0){
        cc=kBBRv2Ecn;
        ecn=true;
    }else if(algo.compare("cubic")==0){
        cc=kCubicBytes;
    }else if(algo.compare("c2tcp")==0){
        cc=kC2TcpBytes;
    }else if(algo.compare("dctcp")==0){
        cc=kDctcp;
        ecn=true;
    }else if(algo.compare("bbrplus")==0){
        cc=kBBRPlus;
    }else if(algo.compare("hsr")==0){
        cc=kHighSpeedRail;
    }else if(algo.compare("tsu")==0){
        cc=kTsunami;
    }else if(algo.compare("copa")==0){
        cc=kCopa;
    }else if(algo.compare("viva")==0){
        cc=kVivace;
    }else if(algo.compare("elastic")==0){
        cc=kElastic;
    }
    
    std::string delimiter="_";
    std::string prefix;
    if(loss_integer>0){
        prefix=instance+delimiter+aqm+delimiter+algo+delimiter+"l"+std::to_string(loss_integer)+delimiter;
    }else{
        prefix=instance+delimiter+aqm+delimiter+algo+delimiter;
    }
    
    std::cout<<prefix<<std::endl;
    SetRenoBeta(reno_beta);

    
    uint32_t max_bps=0;
    uint32_t sender_id=1;
    int test_pair=1;
    uint16_t sendPort=1000;
    uint16_t recvPort=5000;
    std::string log;
    float appStart=start_time;
    float appStop=end_time;
    int flow_count=0;
    
    std::vector<std::unique_ptr<DqcTrace>> traces;
    log=prefix+std::to_string(test_pair);
    std::unique_ptr<DqcTrace> trace;
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT); 
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    flow_count++;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;

    traces.push_back(std::move(trace));
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    flow_count++;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
  
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);    
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    flow_count++;
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    flow_count++;
    assert(appStop>appStart);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);        
    InstallDqc(cc,topo.Get(8),topo.Get(9),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);  
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    flow_count++;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(8),topo.Get(9),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);    
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    flow_count++;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(8),topo.Get(9),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
    
    Simulator::Stop (Seconds(sim_dur+5));
    Simulator::Run ();
    Simulator::Destroy();
    traces.clear();
    stat->Flush(bottleBw,sim_dur);
}
void inter_fair_topo(int ins,std::string aqm,std::string algo,DqcTraceState *stat,int sim_time=200,int loss_integer=0){
    std::string instance=std::to_string(ins);
    int links=sizeof(linkTable)/sizeof(linkTable[0]);
    uint16_t hosts=0;
    std::set<int> hostsTable;
    for(int i=0;i<links;i++){
        hostsTable.insert(linkTable[i].src);
        hostsTable.insert(linkTable[i].dst);
    }
    double sim_dur=sim_time;
    int start_time=0;
    int end_time=sim_time;
    hosts=hostsTable.size();
    int bottleIndex=1;
    uint32_t capacity=12000000;
    uint32_t bottleBw=capacity;
    uint32_t bottleDelay=5;
    uint32_t bottleQdelay=60;
    float reno_beta=0.5;
    uint32_t rtt=30; 
    uint32_t red_delay=30;
    if(instance==std::string("1")){
        bottleBw=capacity;
        bottleDelay=5;
        bottleQdelay=45;
        rtt=30;
        red_delay=bottleQdelay;
        codelTargetDelay=30; 
    }else if(instance==std::string("2")){
        bottleBw=capacity;
        bottleDelay=5;
        bottleQdelay=60;
        rtt=30;
        red_delay=bottleQdelay;
        codelTargetDelay=40;         
    }else if(instance==std::string("3")){
        bottleBw=capacity;
        bottleDelay=5;
        bottleQdelay=75; 
        rtt=30;
        red_delay=bottleQdelay;
        codelTargetDelay=50;        
    }else if(instance==std::string("4")){
        bottleBw=capacity;
        bottleDelay=5;
        bottleQdelay=90;
        rtt=30;
        red_delay=bottleQdelay;
        codelTargetDelay=60;  
    }else if(instance==std::string("5")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=90;
        rtt=60;
        red_delay=bottleQdelay;
        codelTargetDelay=60; 
    }else if(instance==std::string("6")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=120;
        rtt=60;
        red_delay=bottleQdelay;
        codelTargetDelay=80;         
    }else if(instance==std::string("7")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=150; 
        rtt=60;
        red_delay=bottleQdelay;
        codelTargetDelay=100;        
    }else if(instance==std::string("8")){
        bottleBw=capacity;
        bottleDelay=10;
        bottleQdelay=180;
        rtt=60;
        red_delay=bottleQdelay;
        codelTargetDelay=120;  
    }else{return ;}  
    uint32_t bps=4*bottleBw;
    uint32_t delay=bottleDelay;
    uint32_t qDelay=4*bottleQdelay;
    

    LinkAqmType bottleAqm=AQM_DROP;
    if(aqm.compare("codel")==0){
        bottleAqm=AQM_CODEL;       
    }else if(aqm.compare("red")==0){
        bottleAqm=AQM_RED;
    }else{
        aqm=std::string("drop");
    }    

    for(int i=0;i<links;i++){
        uint16_t src=linkTable[i].src;
        uint16_t dst=linkTable[i].dst;
        SetLinkProperty(&linkProperty[i],src,dst,bps,delay,qDelay,rtt);
        if(bottleIndex==i){
            linkProperty[i].bps=bottleBw;
            linkProperty[i].msDelay=bottleDelay;
            linkProperty[i].msQdelay=bottleQdelay;
            linkProperty[i].aqm=bottleAqm;
            linkProperty[i].loss_integer=loss_integer;
        }
    }
    uint32_t mtu=1500;
    NodeContainer topo=BuildTopology(hosts,links,linkProperty,bottleIndex,mtu,red_delay);
    
    dqc::CongestionControlType cc=kRenoBytes;
    dqc::CongestionControlType cc1=kRenoBytes;
    bool ecn=false;
    if(algo.compare("bbrvsreno")==0){
        cc=kBBR;
        cc1=kRenoBytes;
    }else if(algo.compare("bbrvscubic")==0){
        cc=kBBR;
        cc1=kCubicBytes;
    }else if(algo.compare("bbrdvsreno")==0){
        cc=kBBRD;
        cc1=kRenoBytes;
    }else if(algo.compare("bbrdvscubic")==0){
        cc=kBBRD;
        cc1=kCubicBytes;
    }else if(algo.compare("bbrplusvsreno")==0){
        cc=kBBRPlus;
        cc1=kRenoBytes;
    }else if(algo.compare("bbrplusvscubic")==0){
        cc=kBBRPlus;
        cc1=kCubicBytes;
    }else if(algo.compare("quicbbr2vsreno")==0){
        cc=kBBRv2;
        cc1=kRenoBytes;
    }else if(algo.compare("quicbbr2vscubic")==0){
        cc=kBBRv2;
        cc1=kCubicBytes;
    }else if(algo.compare("hsrvsreno")==0){
        cc=kHighSpeedRail;
        cc1=kRenoBytes;
    }else if(algo.compare("hsrvscubic")==0){
        cc=kHighSpeedRail;
        cc1=kCubicBytes;
    }else if(algo.compare("tsuvsreno")==0){
        cc=kTsunami;
        cc1=kRenoBytes;
    }else if(algo.compare("tsuvscubic")==0){
        cc=kTsunami;
        cc1=kCubicBytes;
    }else if(algo.compare("c2tcpvsreno")==0){
        cc=kC2TcpBytes;
        cc1=kRenoBytes;
    }else if(algo.compare("c2tcpvscubic")==0){
        cc=kC2TcpBytes;
        cc1=kCubicBytes;
    }else if(algo.compare("copavsreno")==0){
        cc=kCopa;
        cc1=kRenoBytes;
    }else if(algo.compare("copavscubic")==0){
        cc=kCopa;
        cc1=kCubicBytes;
    }else if(algo.compare("vivavsreno")==0){
        cc=kVivace;
        cc1=kRenoBytes;
    }else if(algo.compare("vivavscubic")==0){
        cc=kVivace;
        cc1=kCubicBytes;
    }else if(algo.compare("elasticvsreno")==0){
        cc=kElastic;
        cc1=kRenoBytes;
    }else if(algo.compare("elasticvscubic")==0){
        cc=kElastic;
        cc1=kCubicBytes;
    }else{
        return ;
    }   
    std::string delimiter="_";
    std::string prefix=instance+delimiter+aqm+delimiter+algo+delimiter;
    std::cout<<prefix<<std::endl;
    SetRenoBeta(reno_beta);
    
    uint32_t max_bps=0;
    uint32_t sender_id=1;
    int test_pair=1;
    uint16_t sendPort=1000;
    uint16_t recvPort=5000;
    std::string log;
    float appStart=start_time;
    float appStop=end_time;
    int flow_count=0;
    int gap=0;
    std::vector<std::unique_ptr<DqcTrace>> traces;
    log=prefix+std::to_string(test_pair);
    std::unique_ptr<DqcTrace> trace;
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT); 
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
  
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);    
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);        
    InstallDqc(cc1,topo.Get(8),topo.Get(9),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);  
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc1,topo.Get(8),topo.Get(9),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);    
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc1,topo.Get(8),topo.Get(9),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
    
    Simulator::Stop (Seconds(sim_dur+5));
    Simulator::Run ();
    Simulator::Destroy();
    traces.clear();
    stat->Flush(bottleBw,sim_dur);
}
void rtt_unfair_topo(int ins,std::string aqm,std::string algo,DqcTraceState *stat,int sim_time=200,int loss_integer=0){
    std::string instance=std::to_string(ins);
    int links=sizeof(linkTable)/sizeof(linkTable[0]);
    uint16_t hosts=0;
    std::set<int> hostsTable;
    for(int i=0;i<links;i++){
        hostsTable.insert(linkTable[i].src);
        hostsTable.insert(linkTable[i].dst);
    }
    double sim_dur=sim_time;
    int start_time=0;
    int end_time=sim_time;
    hosts=hostsTable.size();
    int bottleIndex=1;
    uint32_t capacity=12000000;
    uint32_t bottleBw=capacity;
    uint32_t bottleDelay=5;
    uint32_t bottleQdelay=60;
    uint32_t rtt=30;
    uint32_t red_delay=30;
    float reno_beta=0.5;
    LinkAqmType bottleAqm=AQM_DROP;
    if(aqm.compare("codel")==0){
        bottleAqm=AQM_CODEL;       
    }else if(aqm.compare("red")==0){
        bottleAqm=AQM_RED;
    }else{
        aqm=std::string("drop");
    } 
    
    int bottomArmIndex=8;//9 10
    uint32_t bottomArmDelay=5;
    if(instance==std::string("1")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=10;
        bottomArmIndex=8;
        rtt=40;
        bottleQdelay=rtt*3/2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3; 
    }else if(instance==std::string("2")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=10;
        bottomArmIndex=8;
        rtt=40;
        bottleQdelay=rtt*2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;        
    }else if(instance==std::string("3")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=15;
        bottomArmIndex=8;
        rtt=50;
        bottleQdelay=rtt*3/2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;      
    }else if(instance==std::string("4")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=15;
        bottomArmIndex=8;
        rtt=50;
        bottleQdelay=rtt*2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3; 
    }else if(instance==std::string("5")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=20;
        bottomArmIndex=9;
        rtt=70;
        bottleQdelay=rtt*3/2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;  
    }else if(instance==std::string("6")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=20;
        bottomArmIndex=9;
        rtt=70;
        bottleQdelay=rtt*2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;         
    }else if(instance==std::string("7")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=25;
        bottomArmIndex=9;
        rtt=80;
        bottleQdelay=rtt*3/2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;       
    }else if(instance==std::string("8")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=25;
        bottomArmIndex=9;
        rtt=80;
        bottleQdelay=rtt*2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;   
    }else if(instance==std::string("9")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=30;
        bottomArmIndex=9;
        rtt=90;
        bottleQdelay=rtt*3/2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;       
    }else if(instance==std::string("10")){
        bottleBw=capacity;
        bottleDelay=5;
        bottomArmDelay=30;
        bottomArmIndex=9;
        rtt=90;
        bottleQdelay=rtt*2;
        red_delay=bottleQdelay;
        codelTargetDelay=bottleQdelay*2/3;   
    }else{return ;}  
    
    uint32_t bps=4*bottleBw;
    uint32_t delay=bottleDelay;
    uint32_t qDelay=4*bottleQdelay;
    
   

    for(int i=0;i<links;i++){
        uint16_t src=linkTable[i].src;
        uint16_t dst=linkTable[i].dst;
        SetLinkProperty(&linkProperty[i],src,dst,bps,delay,qDelay,rtt);
        if(bottleIndex==i){
            linkProperty[i].bps=bottleBw;
            linkProperty[i].msDelay=bottleDelay;
            linkProperty[i].msQdelay=bottleQdelay;
            linkProperty[i].aqm=bottleAqm;
            linkProperty[i].loss_integer=loss_integer;
        }
        if(bottomArmIndex==i){
            linkProperty[i].msDelay=bottomArmDelay;
        }
    }
    uint32_t mtu=1500;
    NodeContainer topo=BuildTopology(hosts,links,linkProperty,bottleIndex,mtu,red_delay);
    
    dqc::CongestionControlType cc=kRenoBytes;
    bool ecn=false;
    if(algo.compare("bbr")==0){
        cc=kBBR;
    }else if(algo.compare("bbrd")==0){
        cc=kBBRD;
    }else if(algo.compare("bbrplus")==0){
        cc=kBBRPlus;
    }else if(algo.compare("quicbbr2")==0){
        cc=kBBRv2;
    }else if(algo.compare("quicbbr2ecn")==0){
        cc=kBBRv2Ecn;
        ecn=true;
    }else if(algo.compare("hsr")==0){
        cc=kHighSpeedRail;
    }else if(algo.compare("tsu")==0){
        cc=kTsunami;
    }else if(algo.compare("reno")==0){
        cc=kRenoBytes;
    }else if(algo.compare("cubic")==0){
        cc=kCubicBytes;
    }else if(algo.compare("dctcp")==0){
        cc=kDctcp;
        ecn=true;
    }else if(algo.compare("c2tcp")==0){
        cc=kC2TcpBytes;
    }else if(algo.compare("copa")==0){
        cc=kCopa;
    }else if(algo.compare("viva")==0){
        cc=kVivace;
    }else if(algo.compare("elastic")==0){
        cc=kElastic;
    }else{
        return ;
    }   
    std::string delimiter="_";
    std::string prefix=instance+delimiter+aqm+delimiter+algo+delimiter;
    std::cout<<prefix<<std::endl;
    SetRenoBeta(reno_beta);
 
    uint32_t max_bps=0;
    uint32_t sender_id=1;
    int test_pair=1;
    uint16_t sendPort=1000;
    uint16_t recvPort=5000;
    std::string log;
    float appStart=start_time;
    float appStop=end_time;
    int flow_count=0;
    int gap=0;
    std::vector<std::unique_ptr<DqcTrace>> traces;
    log=prefix+std::to_string(test_pair);
    std::unique_ptr<DqcTrace> trace;
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT); 
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
  
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);    
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(0),topo.Get(1),sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    Ptr<Node> receiver=topo.Get(9);
    if(instance==std::string("7")||instance==std::string("8")||instance==std::string("9")||
    instance==std::string("10")){
        receiver=topo.Get(10);
    }
    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);        
    InstallDqc(cc,topo.Get(8),receiver,sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);  
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(8),receiver,sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    log=prefix+std::to_string(test_pair);
    stat->ReisterAvgDelayId(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_STAT);    
    appStart=start_time+flow_count*gap;
    appStop=end_time-flow_count*gap;
    assert(appStop>appStart);
    InstallDqc(cc,topo.Get(8),receiver,sendPort,recvPort,appStart+0.1,appStop,trace.get(),stat,max_bps,sender_id,ecn);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
    
    Simulator::Stop (Seconds(sim_dur+5));
    Simulator::Run ();
    Simulator::Destroy();
    traces.clear();
    stat->Flush(bottleBw,sim_dur);    
}
int run_same_fairness(){
    int sim_time=1200;
    int total=9;
    char *aqms[]={"red","codel"};
    char *algos[]={"reno","dctcp","quicbbr2","quicbbr2ecn","cubic","elastic","copa","viva","c2tcp","bbr","bbrd","bbrplus",
		   "hsr","tsu"};
    std::string delimiter="_";
    std::cout<<sizeof(algos)/sizeof(algos[0])<<std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    for(int a=0;a<(int)sizeof(aqms)/sizeof(aqms[0]);a++){
        for(int c=0;c<(int)sizeof(algos)/sizeof(algos[0]);c++){
            std::string aqm=std::string(aqms[a]);
            std::string cong=std::string(algos[c]);
            if(aqm.compare("drop")==0){
                if(cong.compare("dctcp")==0||cong.compare("quicbbr2ecn")==0){
                    continue;
                }                
            }
            auto inner_start = std::chrono::high_resolution_clock::now();
            std::string name=aqm+delimiter+cong;
            std::unique_ptr<DqcTraceState> stat;
            stat.reset(new DqcTraceState(name));
            for(int i=0;i<total;i++){
                same_fair_topo(i+1,aqm,cong,stat.get(),sim_time);
            }
            auto inner_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> tm = inner_end - inner_start;
            std::chrono::duration<double, std::ratio<60>> minutes =inner_end- inner_start;
            stat->RecordRuningTime(tm.count(),minutes.count());
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> tm = end - start;
    std::cout << "millis: " << tm.count()<< std::endl;
    std::chrono::duration<double, std::ratio<60>> minutes = end - start;
    std::cout<<"minutes: " << minutes.count() <<std::endl;
    return 0;
}
int run_inter_fair(){
    int sim_time=1200;
    int total=8;
    char *aqms[]={"red"};
    char *algos[]={"bbrvsreno","bbrvscubic","bbrdvsreno","bbrdvscubic","bbrplusvsreno","bbrplusvscubic",
    "quicbbr2vsreno","quicbbr2vscubic","hsrvsreno","hsrvscubic","tsuvsreno","tsuvscubic",
    "c2tcpvsreno","c2tcpvscubic","copavsreno","copavscubic","vivavsreno","vivavscubic",
    "elasticvsreno","elasticvscubic"} ; //1183.65+2495.11
    std::string delimiter="_";
    std::cout<<sizeof(algos)/sizeof(algos[0])<<std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    for(int a=0;a<(int)sizeof(aqms)/sizeof(aqms[0]);a++){
        for(int c=0;c<(int)sizeof(algos)/sizeof(algos[0]);c++){
            std::string aqm=std::string(aqms[a]);
            std::string cong=std::string(algos[c]);
            std::string name=aqm+delimiter+cong;
            std::unique_ptr<DqcTraceState> stat;
            stat.reset(new DqcTraceState(name));
            auto inner_start = std::chrono::high_resolution_clock::now();
            for(int i=0;i<total;i++){
                inter_fair_topo(i+1,aqm,cong,stat.get(),sim_time);
            }
            auto inner_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> tm = inner_end - inner_start;
            std::chrono::duration<double, std::ratio<60>> minutes =inner_end- inner_start;
            stat->RecordRuningTime(tm.count(),minutes.count());            
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> tm = end - start;
    std::cout << "millis: " << tm.count()<< std::endl;
    std::chrono::duration<double, std::ratio<60>> minutes = end - start;
    std::cout<<"minutes: " << minutes.count() <<std::endl;
    return 0;    
}
int run_rtt_unfair(){
    int sim_time=1200;
    int total=10;
    char *aqms[]={"drop","red","codel"};
    char *algos[]={"reno","dctcp","quicbbr2","quicbbr2ecn","cubic","elastic","copa","viva","c2tcp","bbr","bbrd","bbrplus",
		   "hsr","tsu"};

    std::string delimiter="_";
    std::cout<<sizeof(algos)/sizeof(algos[0])<<std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    for(int a=0;a<(int)sizeof(aqms)/sizeof(aqms[0]);a++){
        for(int c=0;c<(int)sizeof(algos)/sizeof(algos[0]);c++){
            std::string aqm=std::string(aqms[a]);
            std::string cong=std::string(algos[c]);
            if(aqm.compare("drop")==0){
                if(cong.compare("dctcp")==0||cong.compare("quicbbr2ecn")==0){
                    continue;
                }                
            }
            auto inner_start = std::chrono::high_resolution_clock::now();
            std::string name=aqm+delimiter+cong;
            std::unique_ptr<DqcTraceState> stat;
            stat.reset(new DqcTraceState(name));
            for(int i=0;i<total;i++){
                rtt_unfair_topo(i+1,aqm,cong,stat.get(),sim_time);
            }
            auto inner_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> tm = inner_end - inner_start;
            std::chrono::duration<double, std::ratio<60>> minutes =inner_end- inner_start;
            stat->RecordRuningTime(tm.count(),minutes.count());
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> tm = end - start;
    std::cout << "millis: " << tm.count()<< std::endl;
    std::chrono::duration<double, std::ratio<60>> minutes = end - start;
    std::cout<<"minutes: " << minutes.count() <<std::endl;
    return 0;
}
int run_random_loss(){   
    int sim_time=1200;
    int total=9;
    char *aqms[]={"codel"};//"red","codel"
    char *algos[]={"reno","dctcp","quicbbr2","quicbbr2ecn","cubic","elastic","copa","viva","c2tcp","bbr","bbrd","bbrplus",
		   "hsr","tsu"};
    int loss_vec[]={10,15,20,25,30,35,40,45,50,55,60,0};
    std::string delimiter="_";
    std::cout<<sizeof(algos)/sizeof(algos[0])<<std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    for(int a=0;a<(int)sizeof(aqms)/sizeof(aqms[0]);a++){
        for(int c=0;c<(int)sizeof(algos)/sizeof(algos[0]);c++){
            std::string aqm=std::string(aqms[a]);
            std::string cong=std::string(algos[c]);
            if(aqm.compare("drop")==0){
                if(cong.compare("dctcp")==0||cong.compare("quicbbr2ecn")==0){
                    continue;
                }                
            }
            auto inner_start = std::chrono::high_resolution_clock::now();
            std::string name=aqm+delimiter+cong;
            std::unique_ptr<DqcTraceState> stat;
            stat.reset(new DqcTraceState(name));
            for(int l=0;l<(int)sizeof(loss_vec)/sizeof(loss_vec[0]);l++){
                for(int i=0;i<total;i++){
                    same_fair_topo(i+1,aqm,cong,stat.get(),sim_time,loss_vec[l],true);
                }                
            }
            auto inner_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> tm = inner_end - inner_start;
            std::chrono::duration<double, std::ratio<60>> minutes =inner_end- inner_start;
            stat->RecordRuningTime(tm.count(),minutes.count());
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> tm = end - start;
    std::cout << "millis: " << tm.count()<< std::endl;
    std::chrono::duration<double, std::ratio<60>> minutes = end - start;
    std::cout<<"minutes: " << minutes.count() <<std::endl;
    return 0;
}
int main(int argc, char *argv[]){
    //run_same_fairness();
    //run_inter_fair();
    //run_rtt_unfair();
    run_random_loss();
    return 0;
}
