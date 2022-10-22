#include "ns3/application-container.h"
#include "ns3/core-module.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/node-container.h"
#include "ns3/node.h"

#include <memory>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>

#include "TcpPktMeta.h"
#include "FlowTable.h"
#include "MultiLevelTable.h"
#include "MakeCallbackHelper.h"


using namespace ns3;
using std::string;
using std::vector;
using std::pair;

namespace fs = std::filesystem;


string linkRate = "100Gbps";
string linkDelay = "500ns";
milliseconds TraffDuration = 1000ms;
int zip = 1;

void GenPktTrace(string traffFilename, string pktTraceFilename) {
    Time::SetResolution (Time::NS);
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue {1440});
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue {Seconds(1)});
    // Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue{QueueSize {"4096p"}});

    Time measureDuration = MilliSeconds(TraffDuration.count() / 2) / zip;
    Time measureEndTime = Seconds(1) + MilliSeconds(TraffDuration.count()) / zip;
    Time measureStartTime = measureEndTime - measureDuration;

    std::ifstream traffFile{traffFilename};
    if (!traffFile.is_open()) {
        std::cout << "Failed to open " << traffFilename << std::endl;
        return;
    }
    int flowCnt = 0;
    traffFile >> flowCnt;
    int senderCnt = (flowCnt + 64999) / 65000;


    // build topo
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
    InetSocketAddress sinkAddr{Ipv4Address::GetAny(), recvPort};
    PacketSinkHelper sink{"ns3::TcpSocketFactory", sinkAddr};
    ApplicationContainer sinkApps = sink.Install(receiverNode);
    sinkApps.Start(Seconds (0));


    // read flows
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


    // trace
    std::ofstream pktTraceFile{pktTraceFilename, std::ios::binary};
    if (!pktTraceFile.is_open()) {
        std::cout << "Failed to open " << pktTraceFilename << std::endl;
        return;
    }

    FlowStats flowStats;
    flowStats.setStatsStartTime(measureStartTime);
    int64_t totalTxPktCnt = 0;
    int64_t totalTxByteCnt = 0;
    int64_t caredTxPktCnt = 0;
    int64_t caredTxByteCnt = 0;
    int64_t txPktSizeHist[16] = {0};
    Time prevTs{0};
    auto txCb = [&](Ptr<const Packet> pkt) {
        Time now = Now();
        totalTxPktCnt++;
        totalTxByteCnt += pkt->GetSize();
        if (now >= measureStartTime) {
            caredTxPktCnt++;
            caredTxByteCnt += pkt->GetSize();
        }

        auto i = pkt->GetSize() / 100;
        txPktSizeHist[i]++;

        std::optional pktMeta = TcpPktMetadata::FromPppPkt(pkt, now);
        if (!pktMeta.has_value()) {
            return;
        }
        flowStats.Record(pktMeta.value());
        pktMeta->WriteToFstream(pktTraceFile);
    };
    auto ns3Callback = MakeCallbackFromCallable (txCb);
    receiverSidePort->TraceConnectWithoutContext("PhyTxBegin", ns3Callback);


    Simulator::Stop(measureEndTime);
    Simulator::Run ();

    pktTraceFile.close();

    std::cout << "TX Total: "
            << totalTxPktCnt << " pkts, " << totalTxByteCnt << " B" << std::endl;
    std::cout << "TX from " << measureStartTime.GetSeconds() << "s"
            << " to " <<  measureEndTime.GetSeconds() << "s: "
            << caredTxPktCnt << " pkts, " << caredTxByteCnt << " B" << std::endl;
    flowStats.PrintStats();

    std::cout << "\n\n======== Packet Size Distribution ========\n";
    for (int i = 0; i < 16; i++) {
        std::cout << i * 100 << "~" << (i+1)*100 << ": " << txPktSizeHist[i] << std::endl;
    }

    Simulator::Destroy ();
}


void
run (string pktTraceFilename, vector<MultiLevelTable::Config> tableConfigs)
{
    nanoseconds statsDuration = nanoseconds{TraffDuration} / ( 2 * zip);
    nanoseconds statsEndTs = 1s + nanoseconds{TraffDuration} / zip;
    nanoseconds statsBeginTs = statsEndTs - statsDuration;

    vector<std::unique_ptr<FlowTable>> flowTables;
    for (int sz : {4'000, 20'000, 40'000, 80'000, 200'000}) {
        auto tbl = std::make_unique<FlowTable>(sz, 1'000us);
        tbl->SetStatsBeginTs(statsBeginTs);
        flowTables.push_back(std::move(tbl));
    }

    vector<std::unique_ptr<MultiLevelTable>> multiLevelTables;
    for (const auto &cfg : tableConfigs) {
        auto tbl = std::make_unique<MultiLevelTable>(cfg);
        tbl->SetStatsBeginTs(statsBeginTs);
        multiLevelTables.push_back(std::move(tbl));
    }


    std::ifstream pktTraceFile{pktTraceFilename, std::ios::binary};
    if (!pktTraceFile.is_open()) {
        std::cout << "Failed to open " << pktTraceFilename << std::endl;
        return;
    }

    int64_t totalPktCnt = 0;
    int64_t caredPktCnt = 0;
    int64_t caredPhyByteCnt = 0;
    while (1) {
        std::optional pktMeta = TcpPktMetadata::FromFstream(pktTraceFile);
        if (!pktMeta.has_value()) {
            break;
        }
        totalPktCnt++;

        nanoseconds now = pktMeta->timestamp;
        if (now >= statsBeginTs) {
            caredPktCnt++;
            caredPhyByteCnt += pktMeta->phyPktSize;
        }

        for (auto &tbl : flowTables) {
            tbl->DoRecord(pktMeta.value());
        }
        for (auto &tbl : multiLevelTables) {
            tbl->DoRecord(pktMeta.value());
        }
    }

    std::cout << "totalPktCnt=" << totalPktCnt
            << ", caredPktCnt=" << caredPktCnt
            << ", caredPhyByteCnt" << caredPhyByteCnt
            << "\n\n";

    for (const auto &tbl : flowTables) {
        tbl->PrintStats();
    }
    std::cout << "\n\n\n\n\n";
    for (auto &tbl : multiLevelTables) {
        tbl->PrintStats();
    }
}


int
main (int argc, char *argv[])
{
    string traffModel{"AliStorage"};
    string mode{"run"};

    CommandLine cmd (__FILE__);
    cmd.AddValue("traff", "traffic model (e.g. AliStorage, GoogleRPC, ...)", traffModel);
    cmd.AddValue("zip", "zip ratio (e.g. 1, 2, 4, ...)", zip);
    cmd.AddNonOption("mode", "'run' or 'genTrace'", mode);
    cmd.Parse (argc, argv);

    if (traffModel == "AliStorage") {
        TraffDuration = 1000ms;
    } else if (traffModel == "GoogleRPC") {
        TraffDuration = 320ms;
    } else {
        std::cerr << "traffic model not expected\n";
        exit(1);
    }

    std::cout << "========"
            << " model=" << traffModel
            << "-" << zip << "x"
            << " ========\n";

    std::ostringstream oss;
    oss << "scratch/measure-sim/pktTrace-" << traffModel << "-" << zip << "x.bin";
    string pktTraceFilename = oss.str();
    string traffFilename = "scratch/measure-sim/traff-" + traffModel + "-100Gbps.txt";

    if (mode == "genTrace") {
        GenPktTrace(traffFilename, pktTraceFilename);
        return 0;
    } else if (mode != "run") {
        std::cerr << "unexpected mode '" << mode << "' (should be 'run' or 'genTrace')\n";
    }

    if (!fs::exists(pktTraceFilename)) {
        std::cerr << "pkt trace file not found. generating it...\n";
        GenPktTrace(traffFilename, pktTraceFilename);
    }

    vector<MultiLevelTable::Config> tableConfigs;
    MultiLevelTable::Config config;
    config.ttl = 1ms;
    for (double alpha : {-1.0, 0.25, 0.5, 0.75, 1.0}) {
        config.alpha = alpha;
        for (bool diffHashFunc : {false, true}) {
            config.diffHashFunc = diffHashFunc;
            for (int colCnt : {2, 3, 4}) {
                config.colCnt = colCnt;
                for (int rowCnt : {4'000, 20'000, 40'000, 80'000, 200'000}) {
                    config.rowCnt = rowCnt;
                    tableConfigs.push_back(config);
                }
            }
        }
    }
    run(pktTraceFilename, tableConfigs);
}
