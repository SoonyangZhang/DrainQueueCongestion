# DrainQueueCongestion
## Algorithms  
Congestion control algorithms evaluation on ns3  
code is referenced from quic protocol for simulation purpose.   
Implemented congestion control algorithms:  
- [x] Reno cubic veno westwood c2tcp elastic  
- [x] vegas tcp-lp copa  
- [x] BBR PCC BBRv2(ecn)  
- [x] DCTCP(ecn)  


Supported multipath congstion control algorithms:  
- [x] lia wvegas olia balia  
- [x] couple BBR  
## Configuration  
As for ns3 test case, the wscript gives clear hint how to arrange this file  
in the right position in ns3.  
And add the CPLUS_INCLUDE_PATH flag in /etc/profile, for example:  
```
export DQC=/home/zsy/C_Test/ns-allinone-3.xx/ns-3.xx/src/dqc/model/thirdparty  
export CPLUS_INCLUDE_PATH=CPLUS_INCLUDE_PATH:$DQC/include/:$DQC/congestion/:$DQC/logging/  
```
The path /home/zsy/C_Test/ is where I put ns-allinone-3.xx under, substituting it with your ns3 path.  
Create a file named "traces" under /xx/xx/ns-allinone-3.xx/ns-3.xx/ for data collection.  
## Run  
Run simulation on ns3.26:  
```
sudo su  
source /etc/profile  
./waf --run "scratch/bbr-var-eva-3.26 --it=1 --cc=bbr"  
```
Of cource, the project can be running on newer version of ns3,  
as long as the topology is built. For example, on ns3.30.  
```
sudo su  
source /etc/profile  
./waf --run "scratch/bbr-var-eva-3.30 --it=1 --cc=bbr"  
```
The difference is only in BuildExampleTopo function.  
## Trace  
In bbr-var-eva-3.30.cc, the enable trace flag is:   
```
trace1.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_BW);  
```
DqcTrace will trace onw way tranmission delay once flag E_DQC_OWD is enabled,  
and the callback function is registered in InstallDqc function.  
```
recvApp->SetOwdTraceFuc(MakeCallback(&DqcTrace::OnOwd,trace));  
```
The meaning of data in it_bbr_flowid_owd.txt can be found in DqcTrace::OnOwd.  
```
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
```
the receipt time of a packet, packet number, owd, packet size.  
## Results  
BBR simulation results:   
Test with 3 flow in a point to point channel(3Mbps, one way delay 100ms, max queue length 300ms).  
bandwidth fairness(drain_to_target_(false)):  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/bw.png)  
one way transmission delay  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/delay.png)  
BBR with the parameter (drain_to_target_(true)):  
```
./waf --run "scratch/dqc-test --it=1 --cc=bbrd"  
```
rate dynamic:  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/drain_to_target_bw.png)  
one way transmission delay:  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/drain_to_target_delay.png)  
BBRv2:  
rate dynamic:  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/bbr2-bw.png)  
one way transmission delay:  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/bbr2-owd.png)  
Cubic simulation results:  
bandwidth fairness:  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/cubic_1_bw.png)  
one way delay  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/cubic_1_delay.png)  
The paper on copa: Copa: Practical Delay-Based Congestion Control for the Internet.  
Copa simulation results:  
bandwidth fairness:  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/copa-1-bw-ability.png)  
one way delay  
![avatar](https://github.com/SoonyangZhang/DrainQueueCongestion/blob/master/result/copa-1-delay-ability.png)  
There is a review papar to evaluate the performance of these algorithms(https://arxiv.org/abs/1909.03673).  

