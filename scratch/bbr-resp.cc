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
using namespace ns3;
using namespace dqc;
const uint32_t TOPO_DEFAULT_BW     = 3000000;    // in bps: 3Mbps
const uint32_t TOPO_DEFAULT_PDELAY =      100;    // in ms:   100ms
const uint32_t TOPO_DEFAULT_QDELAY =     300;    // in ms:  300ms
const uint32_t DEFAULT_PACKET_SIZE = 1000;
static int ip=1;
NS_LOG_COMPONENT_DEFINE ("bbr-resp");
const static uint32_t rateArray[]=
{
4000000,
3000000,
2000000,
1000000,
};
class ChangeBw
{
public:
	ChangeBw(Ptr<NetDevice> netdevice){
	m_netdevice=netdevice;
	m_total=sizeof(rateArray)/sizeof(rateArray[0]);
	}
	//ChangeBw(){}
	~ChangeBw(){}
	void Start()
	{
		Time next=Seconds(m_gap);
		m_timer=Simulator::Schedule(next,&ChangeBw::ChangeRate,this);		
	}
	void ChangeRate()
	{
		if(m_timer.IsExpired())
		{
		NS_LOG_INFO(Simulator::Now().GetSeconds()<<" "<<rateArray[m_index]);
		//Config::Set ("/ChannelList/0/$ns3::PointToPointChannel/DataRate",DataRateValue (rateArray[m_index]));
		PointToPointNetDevice *device=static_cast<PointToPointNetDevice*>(PeekPointer(m_netdevice));
		device->SetDataRate(DataRate(rateArray[m_index]));
		m_index=(m_index+1)%m_total;
		Time next=Seconds(m_gap);
		m_timer=Simulator::Schedule(next,&ChangeBw::ChangeRate,this);
		}

	}
private:
	uint32_t m_index{1};
	uint32_t m_gap{20};
	uint32_t m_total{4};
	Ptr<NetDevice>m_netdevice;
	EventId m_timer;
};
static int average_bps=2500000;
static NodeContainer BuildExampleTopo (uint64_t bps,
                                       uint32_t msDelay,
                                       uint32_t msQdelay,bool enable_random_loss)
{
    NodeContainer nodes;
    nodes.Create (2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
    pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (msDelay)));
    auto bufSize = std::max<uint32_t> (DEFAULT_PACKET_SIZE, average_bps * msQdelay / 8000);
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

    // Uncomment to capture simulated traffic
    // pointToPoint.EnablePcapAll ("rmcat-example");

    // disable tc for now, some bug in ns3 causes extra delay
    TrafficControlHelper tch;
    tch.Uninstall (devices);
    if(false){
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
                        uint32_t max_bps=0
)
{
    Ptr<DqcSender> sendApp = CreateObject<DqcSender> (cc_type);
    //Ptr<DqcDelayAckReceiver> recvApp = CreateObject<DqcDelayAckReceiver>();
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
    std::string instance=std::string("10");
    std::string cc_tmp("bbrplus");
	std::string loss_str("0");
    cmd.AddValue ("it", "instacne", instance);
	cmd.AddValue ("cc", "cctype", cc_tmp);
    cmd.Parse (argc, argv);
    int loss_integer=0;
    double loss_rate=loss_integer*1.0/100;
	if(loss_integer>0){
	Config::SetDefault ("ns3::RateErrorModel::ErrorRate", DoubleValue (loss_rate));
	Config::SetDefault ("ns3::RateErrorModel::ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
	Config::SetDefault ("ns3::BurstErrorModel::ErrorRate", DoubleValue (loss_rate));
	Config::SetDefault ("ns3::BurstErrorModel::BurstSize", StringValue ("ns3::UniformRandomVariable[Min=1|Max=3]"));
	}
    //std::string filename("error.log");
    //std::ios::openmode filemode=std::ios_base::out;
    //GlobalStream::Create(filename,filemode);
    LogComponentEnable("dqcsender",LOG_LEVEL_ALL);
	//LogComponentEnable("queue_limit",LOG_LEVEL_ALL);
    LogComponentEnable("proto_connection",LOG_LEVEL_ALL);
    //LogComponentEnable("dqcreceiver",LOG_LEVEL_ALL);
	//LogComponentEnable("dqcdelayackreceiver",LOG_LEVEL_ALL);
	//ns3::LogComponentEnable("proto_pacing",LOG_LEVEL_ALL);
	uint64_t linkBw   = TOPO_DEFAULT_BW;
    uint32_t msDelay  = TOPO_DEFAULT_PDELAY;
    uint32_t msQDelay = TOPO_DEFAULT_QDELAY;
    uint32_t max_bps=0;
	std::string cc_name;
	cc_name="_"+cc_tmp+"_resp_";

    if(instance==std::string("1")){
    	linkBw=4000000;
    	msDelay=50;
    	msQDelay=100;
    }else if(instance==std::string("2")){
    	linkBw=4000000;
    	msDelay=50;
    	msQDelay=150;       
    }else if(instance==std::string("3")){
    	linkBw=4000000;
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
    	linkBw=6000000;
    	msDelay=50;
    	msQDelay=200;       
    }else if(instance==std::string("7")){
    	linkBw=8000000;
    	msDelay=50;
    	msQDelay=150;       
    }else if(instance==std::string("8")){
    	linkBw=10000000;
    	msDelay=50;
    	msQDelay=150;
    }else if(instance==std::string("9")){
    	linkBw=12000000;
    	msDelay=50;
    	msQDelay=150;       
    }
    dqc::CongestionControlType cc=kBBR;/*kQueueLimit kCubicBytes kBBRv2 kBBRPlus kTsunami kHighSpeedRail*/
	if(cc_tmp==std::string("bbr")){
		cc=kBBR;
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("bbrd")){
		cc=kBBR; //drain to target
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("bbrplus")){
		cc=kBBRPlus;
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("bbrv2")){
		cc=kBBRv2;
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("cubic")){
		cc=kCubicBytes;
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("reno")){
		cc=kRenoBytes;
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("hsr")){
		cc=kHighSpeedRail;
		std::cout<<cc_tmp<<std::endl;
	}else if(cc_tmp==std::string("tsu")){
		cc=kTsunami;
		std::cout<<cc_tmp<<std::endl;
	}
	bool enable_random_loss=false;
	if(loss_integer>0){
		enable_random_loss=true;
	}
	NodeContainer nodes = BuildExampleTopo (linkBw, msDelay, msQDelay,enable_random_loss);
	int test_pair=1;
	DqcTrace trace1;
	std::string log=instance+cc_name+std::to_string(test_pair);
	trace1.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort,recvPort,appStart,appStop,&trace1,max_bps);
	
	DqcTrace trace2;
	log=instance+cc_name+std::to_string(test_pair);
	trace2.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(cc,nodes.Get(0),nodes.Get(1),sendPort+1,recvPort+1,appStart,appStop,&trace2,max_bps);

	/*DqcTrace trace3;
	log=instance+cc_name+std::to_string(test_pair);
	trace3.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(kCubicBytes,nodes.Get(0),nodes.Get(1),sendPort+2,recvPort+2,appStart,appStop,&trace3,max_bps);

	DqcTrace trace4;
	log=instance+cc_name+std::to_string(test_pair);
	trace4.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);
	test_pair++;
	InstallDqc(kCubicBytes,nodes.Get(0),nodes.Get(1),sendPort+3,recvPort+3,appStart,appStop,&trace4,max_bps);*/
    Ptr<NetDevice> netDevice=nodes.Get(0)->GetDevice(0);
	ChangeBw change(netDevice);
	change.Start();
    Simulator::Stop (Seconds(simDuration));
    Simulator::Run ();
    Simulator::Destroy();	
    return 0;
}
