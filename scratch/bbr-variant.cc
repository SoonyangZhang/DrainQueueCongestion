/*test quic bbr on ns3 platform*/
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
                        uint32_t max_bps=0,uint32_t id=0
)
{
    Ptr<DqcSender> sendApp = CreateObject<DqcSender> (cc_type);
	sendApp->SetSenderId(id);
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
static double simDuration=300;
uint16_t sendPort=5432;
uint16_t recvPort=5000;
float appStart=0.0;
float appStop=simDuration;
int main(int argc, char *argv[]){
	CommandLine cmd;
    std::string instance=std::string("1");
    std::string cc_tmp("bbr");
	std::string loss_str("0");
    cmd.AddValue ("it", "instacne", instance);
    cmd.Parse (argc, argv);
    int loss_integer=0;
    LogComponentEnable("dqcsender",LOG_LEVEL_ALL);
    LogComponentEnable("proto_connection",LOG_LEVEL_ALL);
	uint64_t linkBw   = TOPO_DEFAULT_BW;
    uint32_t msDelay  = TOPO_DEFAULT_PDELAY;
    uint32_t msQDelay = TOPO_DEFAULT_QDELAY;
    uint32_t max_bps=0;
	std::string cc_name("_all_");	
    if(instance==std::string("1")){
        linkBw=8000000;
        msDelay=50;
        msQDelay=100;
    }else if(instance==std::string("2")){
        linkBw=8000000;
        msDelay=50;
        msQDelay=150;        
    }else{
        linkBw=8000000;
        msDelay=50;
        msQDelay=200;        
    }
    dqc::CongestionControlType cc=kBBR;/*kQueueLimit kCubicBytes kBBRv2 kBBRPlus kTsunami kHighSpeedRail*/
	bool enable_random_loss=false;
	NodeContainer nodes = BuildExampleTopo (linkBw, msDelay, msQDelay,enable_random_loss);
	std::string prefix=instance+cc_name;


	cc=kBBR;
    uint32_t sender_id=1;
	int test_pair=1;
	DqcTrace trace1;
	std::string log=prefix+std::to_string(test_pair);
	trace1.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort,recvPort,appStart,appStop,&trace1,max_bps,sender_id);
    sender_id++;

	cc=kBBRD;
	DqcTrace trace2;
	log=prefix+std::to_string(test_pair);
	trace2.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+1,recvPort+1,appStart,appStop,&trace2,max_bps,sender_id);
    sender_id++;

	cc=kBBRPlus;  
	DqcTrace trace3;
	log=prefix+std::to_string(test_pair);
	trace3.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+2,recvPort+2,appStart,appStop,&trace3,max_bps,sender_id);
    sender_id++;

	cc=kBBRv2;
	DqcTrace trace4;
	log=prefix+std::to_string(test_pair);
	trace4.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+3,recvPort+3,appStart,appStop,&trace4,max_bps,sender_id);
    sender_id++;


	cc=kHighSpeedRail;
	DqcTrace trace5;
	log=prefix+std::to_string(test_pair);
	trace5.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+4,recvPort+4,appStart,appStop,&trace5,max_bps,sender_id);
    sender_id++;


	cc=kTsunami;
	DqcTrace trace6;
	log=prefix+std::to_string(test_pair);
	trace6.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+5,recvPort+5,appStart,appStop,&trace6,max_bps,sender_id);
    sender_id++;

    Simulator::Stop (Seconds(simDuration));
    Simulator::Run ();
    Simulator::Destroy();	
    return 0;
}
