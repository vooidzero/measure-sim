#include "BaseMidDevApp.h"
#include "ns3/application-container.h"
#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"

#include "ns3/csma-module.h"
#include "ns3/node-container.h"
#include "ns3/node.h"

#include <memory>
#include <ostream>
#include <fstream>
#include <string>

#include "FlowAggr.h"
#include "MakeCallbackHelper.h"

using namespace ns3;

std::string linkRate = "25Gbps";
std::string linkDelay = "1us";
constexpr int SENDER_CNT = 8;

void
run (std::string traffFileName, int hashTableSize)
{
    NodeContainer senderNodes{SENDER_CNT};
    auto middleNode = CreateObject<Node>();
    auto receiverNode = CreateObject<Node>();
    NetDeviceContainer senderNetDevices;
    NetDeviceContainer senderSidePorts;

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue {linkRate});
    p2p.SetChannelAttribute ("Delay", StringValue {linkDelay});
    for (int i = 0; i < SENDER_CNT; i++) {
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
    for (int i = 0; i < SENDER_CNT; i++) {
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
    std::ifstream traffFile{traffFileName};
    if (!traffFile.is_open()) {
        std::cout << "Failed to open " << traffFileName << std::endl;
        Simulator::Destroy ();
        return;
    }
    int flowCnt = 0;
    traffFile >> flowCnt;
    if (flowCnt > SENDER_CNT * 65000) {
        std::cout << "ERROR: number of flows(" << flowCnt <<  ") is larger than expected."
                  << "Please use a larger SENDER_CNT\n";
        Simulator::Destroy ();
        return;
    }
    for (int i = 0; i < flowCnt; i++) {
        double ts;
        int srcMachine, dstMachine;
        uint64_t flowSize;
        traffFile >> ts >> srcMachine >> dstMachine >> flowSize;
        genTotalBytes += flowSize;
        uint16_t dstPort = recvPort;
        InetSocketAddress dstSockAddr = {receiverAddr, dstPort};
        BulkSendHelper source{"ns3::TcpSocketFactory", dstSockAddr};
        int sender = i % SENDER_CNT;
        uint16_t srcPort = 13 + ((i / SENDER_CNT) % 65000);
        InetSocketAddress srcSockAddr{Ipv4Address::GetAny(), srcPort};
        source.SetAttribute ("Local", AddressValue{srcSockAddr});
        source.SetAttribute ("MaxBytes", UintegerValue{flowSize});
        ApplicationContainer sourceApps = source.Install(senderNodes.Get(sender));
        sourceApps.Start (Seconds(ts));
    }
    traffFile.close();
    std::cout << "Generate: " << flowCnt << " flows, " << genTotalBytes << " B\n";

    for (uint16_t i = 1; i <= 16; i++) {
        InetSocketAddress sinkAddr{Ipv4Address::GetAny(), i};
        PacketSinkHelper sink{"ns3::TcpSocketFactory", sinkAddr};
        ApplicationContainer sinkApps = sink.Install(receiverNode);
        sinkApps.Start (Seconds (0.0));
    }

    FlowAggr midApp{hashTableSize};
    int64_t txPktCnt = 0;
    int64_t txByteCnt = 0;
    int64_t txPktSizeHist[32] = {0};
    int64_t nextTs = Seconds(1.4).GetNanoSeconds();
    auto txCb = [&](Ptr<const Packet> pkt) {
        txPktCnt++;
        txByteCnt += pkt->GetSize();
        auto i = pkt->GetSize() / 100;
        txPktSizeHist[i]++;
        midApp.HandlePacket(pkt->Copy());
        if (Now().GetNanoSeconds() > nextTs) {
            std::cout << "Until " << NanoSeconds(nextTs).GetSeconds() << " second\n";
            std::cout << "TX: " << txPktCnt << " packets, "
                      << txByteCnt << " B" << std::endl;
            std::cout << "extra: " << midApp.GetExtraPktCnt() << " packets, "
                      << midApp.GetExtraByteCnt() << " B" << std::endl;
            std::cout << "total flows: " << midApp.GetTotalFlowCnt()
                      << ", current: " << midApp.GetCurrFlowCnt()
                      << ", active: " << midApp.PollActiveFlowCnt()
                      << std::endl;
            std::cout << std::endl;
            nextTs += (int64_t)4e8;
        }
    };
    auto ns3Callback = MakeCallbackFromCallable (txCb);
    receiverSidePort->TraceConnectWithoutContext("PhyTxBegin", ns3Callback);

    // =======================================================================================================
    Simulator::Run ();
    Simulator::Destroy ();
    
    std::cout << "TX: " << txPktCnt << " packets, " << txByteCnt << " B" << std::endl;
    std::cout << "extra: " << midApp.GetExtraByteCnt() << " packets, "
              << midApp.GetExtraByteCnt() << " B" << std::endl;
    if (txByteCnt != 0) {
        std::cout << "overhead: " << (double)midApp.GetExtraByteCnt() / txByteCnt << std::endl;
    }
    /*std::cout << "======== Packet Size Distribution ========\n";
    for (int i = 0; i < 16; i++) {
        std::cout << i * 100 << "~" << (i+1)*100 << ": " << txPktSizeHist[i] << std::endl;
    }*/
    
    midApp.PrintFlowDurationStats();
}


int
main (int argc, char *argv[])
{
    CommandLine cmd (__FILE__);
    // cmd.AddValue ("linkRate", "link bandwith", linkRate);
    // cmd.AddValue ("linkDelay", "link delay", linkDelay);
    cmd.Parse (argc, argv);

    Time::SetResolution (Time::NS);
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue {1440});
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue {Seconds(1)});
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue{QueueSize {"4096p"}});

    std::vector<std::string> traffModels {
        "AliStorage",
        "GoogleRPC",
        "WebSearch",
        "FacebookHadoop"
    };
    std::vector<int> memorySize{
        10'000, 100'000, 1'000'000
    };
    for (const auto &traffModel : traffModels) {
        std::string file = "scratch/measure-sim/traff-" + traffModel + ".txt";
        for (auto memSize : memorySize) {
            int tableEntryCnt = memSize / 25;
            std::cout << "\n\n========"
                      << " model=" << traffModel
                      << " memSize=" << memSize
                      << "(tableEntryCnt=" << tableEntryCnt
                      << "========\n";
            run(file, tableEntryCnt);
        }
    }
}