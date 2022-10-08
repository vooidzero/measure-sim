#include "TimeHelper.h"
#include "ns3/application-container.h"
#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"

#include "ns3/csma-module.h"
#include "ns3/node-container.h"
#include "ns3/node.h"

#include <chrono>
#include <memory>
#include <fstream>
#include <string>
#include <vector>

#include "FlowAggr.h"
#include "MakeCallbackHelper.h"


using namespace ns3;
using std::string;
using std::vector;
using std::pair;

int zip = 1;

string linkRate = "25Gbps";
string linkDelay = "1us";

void
run (string traffFileName, vector<pair<int, microseconds>> hashTableArgs)
{
    microseconds measureDuration = microseconds{500ms} / zip;
    microseconds measureEndTime = 1s + microseconds{1s} / zip;
    microseconds measureStartTime = measureEndTime - measureDuration;

    std::ifstream traffFile{traffFileName};
    if (!traffFile.is_open()) {
        std::cout << "Failed to open " << traffFileName << std::endl;
        return;
    }
    int flowCnt = 0;
    traffFile >> flowCnt;
    int senderCnt = (flowCnt + 64999) / 65000;

    NodeContainer senderNodes{(uint32_t)senderCnt};
    auto middleNode = CreateObject<Node>();
    auto receiverNode = CreateObject<Node>();
    NetDeviceContainer senderNetDevices;
    NetDeviceContainer senderSidePorts;

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue {linkRate});
    p2p.SetChannelAttribute ("Delay", StringValue {linkDelay});
    for (int i = 0; i < senderCnt; i++) {
        NetDeviceContainer devs = p2p.Install ({senderNodes.Get(i), middleNode});
        senderNetDevices.Add(devs.Get(0));
        senderSidePorts.Add(devs.Get(1));
    }
    NetDeviceContainer rightNetDevs = p2p.Install ({middleNode, receiverNode});
    Ptr<NetDevice> receiverSidePort = rightNetDevs.Get(0);
    Ptr<NetDevice> receiverNetDev = rightNetDevs.Get(1);
    InternetStackHelper stack;
    stack.Install(senderNodes);
    stack.Install(middleNode);
    stack.Install(receiverNode);

    Ipv4AddressHelper address;
    for (int i = 0; i < senderCnt; i++) {
        uint32_t net = (10U << 24) | (i << 2);
        uint32_t mask = ~((1U << 2) - 1);
        address.SetBase (Ipv4Address{net}, mask);
        address.Assign(senderNetDevices.Get(i));
        address.Assign(senderSidePorts.Get(i));
    }
    address.SetBase ("10.1.0.0", "255.255.255.0");
    auto receiverAddr = address.Assign (receiverNetDev).GetAddress(0);
    address.Assign(receiverSidePort);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    uint16_t recvPort = 9;
    int64_t genTotalBytes = 0;
    for (int i = 0; i < flowCnt; i++) {
        double ts;
        int srcMachine, dstMachine;
        uint64_t flowSize;
        traffFile >> ts >> srcMachine >> dstMachine >> flowSize;
        ts = 1 + (ts - 1) / zip;
        genTotalBytes += flowSize;
        uint16_t dstPort = recvPort;
        InetSocketAddress dstSockAddr = {receiverAddr, dstPort};
        BulkSendHelper source{"ns3::TcpSocketFactory", dstSockAddr};
        int sender = i % senderCnt;
        uint16_t srcPort = 13 + ((i / senderCnt) % 65000);
        InetSocketAddress srcSockAddr{Ipv4Address::GetAny(), srcPort};
        source.SetAttribute ("Local", AddressValue{srcSockAddr});
        source.SetAttribute ("MaxBytes", UintegerValue{flowSize});
        ApplicationContainer sourceApps = source.Install(senderNodes.Get(sender));
        sourceApps.Start (Seconds(ts));

        if (i == flowCnt - 1) {
            std::cout << "lastOne: ts=" << ts << "\n";
        }
    }
    traffFile.close();
    std::cout << "Generate: " << flowCnt << " flows, " << genTotalBytes << " B\n";

    InetSocketAddress sinkAddr{Ipv4Address::GetAny(), recvPort};
    PacketSinkHelper sink{"ns3::TcpSocketFactory", sinkAddr};
    ApplicationContainer sinkApps = sink.Install(receiverNode);
    sinkApps.Start(Seconds (0.0));

    vector<std::unique_ptr<FlowAggr>> midApps;
    for (auto [sz, ttl] : hashTableArgs) {
        midApps.push_back(std::make_unique<FlowAggr>(sz, ttl));
    }
    int64_t totalTxPktCnt = 0;
    int64_t totalTxByteCnt = 0;
    int64_t pastTxPktCnt = 0;
    int64_t pastTxByteCnt = 0;
    int64_t txPktSizeHist[16] = {0};
    microseconds prevTs{0};
    auto txCb = [&](Ptr<const Packet> pkt) {
        microseconds now{Now().GetMicroSeconds()};
        if (prevTs < measureStartTime && now >= measureStartTime) {
            pastTxByteCnt = totalTxByteCnt;
            pastTxPktCnt = totalTxPktCnt;
            for (auto &x : txPktSizeHist) {
                x = 0;
            }
            for (auto &midApp : midApps) {
                midApp->enableStats();
            }
        }
        prevTs = now;

        totalTxPktCnt++;
        totalTxByteCnt += pkt->GetSize();

        auto i = pkt->GetSize() / 100;
        txPktSizeHist[i]++;

        auto copiedPkt = pkt->Copy();
        for (auto &midApp : midApps) {
            midApp->HandlePacket(copiedPkt);
        }
    };
    auto ns3Callback = MakeCallbackFromCallable (txCb);
    receiverSidePort->TraceConnectWithoutContext("PhyTxBegin", ns3Callback);

    Simulator::Stop(toNsTime(measureEndTime));
    // =======================================================================================================
    Simulator::Run ();
    Simulator::Destroy ();

    std::cout << "total TX: " << totalTxPktCnt << " packets, "
              << totalTxByteCnt << " B" << std::endl;
    std::cout << "from " << toNsTime(measureStartTime).GetSeconds() << "s"
            << " to " <<  toNsTime(measureEndTime).GetSeconds() << "s\n";
    std::cout << "TX: " << totalTxPktCnt - pastTxPktCnt << " packets, "
              << totalTxByteCnt - pastTxByteCnt << " B" << std::endl;
    for (int i = 0; i < (int)midApps.size(); i++) {
        auto &midApp = midApps[i];
        std::cout << "======== " << "table=" << hashTableArgs[i].first << "," << hashTableArgs[i].second << " ========\n";
        std::cout << "records: " << midApp->GetRecordCnt()
                << ", expires: " << midApp->GetExpirCnt()
                << ", collisions: " << midApp->GetCollisionCnt()
                << "\n\n\n";
        if (i == (int)midApps.size() - 1) {
            midApp->PrintFlowDurationStats();
        }
    }
    std::cout << "\n\n======== Packet Size Distribution ========\n";
    for (int i = 0; i < 16; i++) {
        std::cout << i * 100 << "~" << (i+1)*100 << ": " << txPktSizeHist[i] << std::endl;
    }
}


int
main (int argc, char *argv[])
{
    CommandLine cmd (__FILE__);
    // cmd.AddValue ("linkRate", "link bandwith", linkRate);
    // cmd.AddValue ("linkDelay", "link delay", linkDelay);
    cmd.AddValue ("zip", "zip ratio (e.g. 1, 2, 4, ...)", zip);
    cmd.Parse (argc, argv);

    Time::SetResolution (Time::NS);
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue {1440});
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue {Seconds(1)});
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue{QueueSize {"4096p"}});

    vector<string> traffModels {
        "AliStorage"
    };
    vector<pair<int, microseconds>> hashTableArgs;
    for (microseconds ttl : {50us, 250us, 1'000us, 50'000us, -1us}) {
        for (int size : {400, 4'000, 40'000}) {
            hashTableArgs.emplace_back(size, ttl);
        }
    }
    
    for (const auto &traffModel : traffModels) {
        string file = "scratch/measure-sim/traff-" + traffModel + ".txt";
        std::cout << "\n\n========" << " model=" << traffModel << " ========\n";
        run(file, hashTableArgs);
    }
}