/*test quic bbr on ns3.26*/
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/dqc-module.h"
#include <string>
#include <iostream>
#include <memory>
using namespace ns3;
using namespace dqc;
const uint32_t TOPO_DEFAULT_BW     = 3000000;    // in bps: 3Mbps
const uint32_t TOPO_DEFAULT_PDELAY =      100;    // in ms:   100ms
const uint32_t TOPO_DEFAULT_QDELAY =     300;    // in ms:  300ms
const uint32_t DEFAULT_PACKET_SIZE = 1000;
static int ip=1;
static NodeContainer BuildExampleTopo (uint64_t bps,
                                       uint32_t msDelay,
                                       uint32_t msQdelay,bool enable_random_loss)
{
    NodeContainer nodes;
    nodes.Create (2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
    pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (msDelay)));
    auto bufSize = std::max<uint32_t> (DEFAULT_PACKET_SIZE, bps * msQdelay / 8000);
    pointToPoint.SetQueue ("ns3::DropTailQueue",
                           "Mode", StringValue ("QUEUE_MODE_BYTES"),
                           "MaxBytes", UintegerValue (bufSize));
    NetDeviceContainer devices = pointToPoint.Install (nodes);

    InternetStackHelper stack;
    stack.Install (nodes);
    Ipv4AddressHelper address;
    std::string nodeip="10.1."+std::to_string(ip)+".0";
    ip++;
    address.SetBase (nodeip.c_str(), "255.255.255.0");
    address.Assign (devices);
    // disable tc for now, some bug in ns3 causes extra delay
    TrafficControlHelper tch;
    tch.Uninstall (devices);
    if(enable_random_loss){
    std::string errorModelType = "ns3::RateErrorModel";
    ObjectFactory factory;
    factory.SetTypeId (errorModelType);
    Ptr<ErrorModel> em = factory.Create<ErrorModel> ();
    devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));		
    }
    return nodes;
}
static void InstallDqc( dqc::CongestionControlType cc_type,
                        Ptr<Node> sender,
                        Ptr<Node> receiver,
                        uint16_t send_port,
                        uint16_t recv_port,
                        float startTime,
                        float stopTime,
                        DqcTrace *trace,
                        uint32_t max_bps=0,uint32_t id=0,uint32_t emucons=2)
{
    Ptr<DqcSender> sendApp = CreateObject<DqcSender> (cc_type);
    sendApp->SetSenderId(id);
    sendApp->SetNumEmulatedConnections(emucons);
    Ptr<DqcReceiver> recvApp = CreateObject<DqcReceiver>();
    sender->AddApplication (sendApp);
    receiver->AddApplication (recvApp);
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
    if(trace){
        sendApp->SetBwTraceFuc(MakeCallback(&DqcTrace::OnBw,trace));
        //sendApp->SetTraceLossPacketDelay(MakeCallback(&DqcTrace::OnLossPacketInfo,trace));
        //sendApp->SetSentSeqTraceFuc(MakeCallback(&DqcTrace::OnSentSeq,trace));
        recvApp->SetOwdTraceFuc(MakeCallback(&DqcTrace::OnOwd,trace));
    }	
}
static void InstallTcp(
                         Ptr<Node> sender,
                         Ptr<Node> receiver,
                         uint16_t port,
                         float startTime,
                         float stopTime
)
{
    // configure TCP source/sender/client
    auto serverAddr = receiver->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();
    BulkSendHelper source{"ns3::TcpSocketFactory",
                           InetSocketAddress{serverAddr, port}};
    // Set the amount of data to send in bytes. Zero is unlimited.
    source.SetAttribute ("MaxBytes", UintegerValue (0));
    source.SetAttribute ("SendSize", UintegerValue (DEFAULT_PACKET_SIZE));

    auto clientApps = source.Install (sender);
    clientApps.Start (Seconds (startTime));
    clientApps.Stop (Seconds (stopTime));

    // configure TCP sink/receiver/server
    PacketSinkHelper sink{"ns3::TcpSocketFactory",
                           InetSocketAddress{Ipv4Address::GetAny (), port}};
    auto serverApps = sink.Install (receiver);
    serverApps.Start (Seconds (startTime));
    serverApps.Stop (Seconds (stopTime));
}
static double simDuration=400;
uint16_t sendPort=5432;
uint16_t recvPort=5000;
float appStart=0.0;
float appStop=simDuration;
int main(int argc, char *argv[]){
    CommandLine cmd;
    std::string instance=std::string("3");
    std::string cc_tmp("bbr");
    std::string loss_str("0");
    cmd.AddValue ("it", "instacne", instance);
    cmd.AddValue ("cc", "cctype", cc_tmp);
    cmd.AddValue ("lo", "loss",loss_str);
    cmd.Parse (argc, argv);
    int loss_integer=std::stoi(loss_str);
    double loss_rate=loss_integer*1.0/100;
    std::cout<<"l "<<loss_integer<<std::endl;
    if(loss_integer>0){
        Config::SetDefault ("ns3::RateErrorModel::ErrorRate", DoubleValue (loss_rate));
        Config::SetDefault ("ns3::RateErrorModel::ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
        Config::SetDefault ("ns3::BurstErrorModel::ErrorRate", DoubleValue (loss_rate));
        Config::SetDefault ("ns3::BurstErrorModel::BurstSize", StringValue ("ns3::UniformRandomVariable[Min=1|Max=3]"));
    }
    LogComponentEnable("dqcsender",LOG_LEVEL_ALL);
    LogComponentEnable("proto_connection",LOG_LEVEL_ALL);
    uint64_t linkBw   = TOPO_DEFAULT_BW;
    uint32_t msDelay  = TOPO_DEFAULT_PDELAY;
    uint32_t msQDelay = TOPO_DEFAULT_QDELAY;
    uint32_t max_bps=0;
    std::string cc_name;
    if(loss_integer>0){
        cc_name="_"+cc_tmp+"_l"+std::to_string(loss_integer)+"_";
    }else{
        cc_name="_"+cc_tmp+"_";
    }
    int emucons=2;
    if(instance==std::string("1")){
        linkBw=5000000;
        msDelay=50;
        msQDelay=100;
    }else if(instance==std::string("2")){
        linkBw=5000000;
        msDelay=50;
        msQDelay=150;        
    }else if(instance==std::string("3")){
        linkBw=5000000;
        msDelay=50;
        msQDelay=200;        
    }else if(instance==std::string("4")){
        linkBw=6000000;
        msDelay=50;
        msQDelay=100;
    }else if(instance==std::string("5")){
        linkBw=6000000;
        msDelay=50;
        msQDelay=150;        
    }else if(instance==std::string("6")){
        linkBw=7000000;
        msDelay=50;
        msQDelay=150;        
    }else if(instance==std::string("7")){
        linkBw=7000000;
        msDelay=100;
        msQDelay=300;        
    }else if(instance==std::string("8")){
        linkBw=8000000;
        msDelay=50;
        msQDelay=200;        
    }else if(instance==std::string("9")){
        linkBw=8000000;
        msDelay=100;
        msQDelay=300;        
    }else if(instance==std::string("10")){
        linkBw=10000000;
        msDelay=50;
        msQDelay=150;        
    }else if(instance==std::string("11")){
        linkBw=10000000;
        msDelay=50;
        msQDelay=200;        
    }else if(instance==std::string("12")){
        linkBw=12000000;
        msDelay=100;
        msQDelay=200;        
    }else if(instance==std::string("13")){
        linkBw=12000000;
        msDelay=100;
        msQDelay=300;        
    }else if(instance==std::string("14")){
        linkBw=15000000;
        msDelay=50;
        msQDelay=150;        
    }
    dqc::CongestionControlType cc=kBBR;/*kQueueLimit kCubicBytes kBBRv2 kBBRPlus kTsunami kHighSpeedRail*/
    if(cc_tmp==std::string("bbr")){
        cc=kBBR;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("bbrd")){
        cc=kBBRD; //drain to target
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("bbrplus")){
        cc=kBBRPlus;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("bbrrand")){
        cc=kBBRRand;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("bbrv2")){
        cc=kBBRv2;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("cubic")){
        cc=kCubicBytes;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("westwood")){
        cc=kWestwood;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("cubicplus")){
        cc=kCubicPlus;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("reno")){
        cc=kRenoBytes;
        emucons=1;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("renoplus")){
        cc=kRenoPlus;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("hsr")){
        cc=kHighSpeedRail;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("tsu")){
        cc=kTsunami;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("copa")){
        cc=kCopa;
        std::cout<<cc_tmp<<std::endl;
    }else if(cc_tmp==std::string("pcc")){
        cc=kPCC;
        std::cout<<cc_tmp<<std::endl;
    }else{
        cc=kBBR;
        std::cout<<cc_tmp<<std::endl;
    }
    bool enable_random_loss=false;
    if(loss_integer>0){
        enable_random_loss=true;
    }
    NodeContainer nodes = BuildExampleTopo (linkBw, msDelay, msQDelay,enable_random_loss);
    std::string prefix=instance+cc_name;
    uint32_t sender_id=1;
    int test_pair=1;
    DqcTrace trace1;
    std::string log=prefix+std::to_string(test_pair);
    trace1.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
    test_pair++;
    InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort,recvPort,appStart,appStop,&trace1,max_bps,sender_id,emucons);
    sender_id++;
    
    
    DqcTrace trace2;
    log=prefix+std::to_string(test_pair);
    trace2.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
    test_pair++;
    InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+1,recvPort+1,appStart+40,appStop,&trace2,max_bps,sender_id,emucons);
    sender_id++;
    
    DqcTrace trace3;
    log=prefix+std::to_string(test_pair);
    trace3.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
    test_pair++;
    InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+2,recvPort+2,appStart+80,200,&trace3,max_bps,sender_id,emucons);
    sender_id++;
    
    DqcTrace trace4;
    log=prefix+std::to_string(test_pair);
    trace4.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
    test_pair++;
    InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+3,recvPort+3,appStart+120,300,&trace4,max_bps,sender_id,emucons);
    sender_id++;

    DqcTrace trace5;
    log=prefix+std::to_string(test_pair);
    trace5.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
    test_pair++;
    InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+4,recvPort+4,appStart+200,appStop,&trace5,max_bps,sender_id,emucons);
    sender_id++;

    Simulator::Stop (Seconds(simDuration));
    Simulator::Run ();
    Simulator::Destroy();
    return 0;
}
