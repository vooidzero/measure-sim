#include "FlowAggr.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ipv4-address.h"
#include "ns3/ppp-header.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/tcp-header.h"
#include <algorithm>
#include <queue>

NS_LOG_COMPONENT_DEFINE ("FlowAggr");

FlowAggr::FlowAggr(int hashTableSize, microseconds ttl)
    : m_hashTableSize{hashTableSize},
    m_ttl{ttl},
    m_hashTable{new Record[hashTableSize]}
{}

int64_t FlowAggr::PollActiveFlowCnt() {
    auto ret = m_activeFlowSet.size();
    m_activeFlowSet.clear();
    return ret;
}

void FlowAggr::OutputRecord(const Record &record) {
    m_recordCnt++;
}

void FlowAggr::DoRecord(const FlowTuple &flow, int byteCnt, bool flush) {
    uint32_t idx = flow.GetHashValue() % m_hashTableSize;
    microseconds now{Now().GetMicroSeconds()};
    auto &cell = m_hashTable[idx];
    if (cell.IsValid()) {
        bool isExpired = (m_ttl > 0us && now - microseconds{cell.startTime} > m_ttl);
        if (flow != cell.flow) {
            m_collisionCnt++;
        } else if (isExpired) {
            m_expirCnt++;
        }
        if (flow != cell.flow || isExpired) {
            OutputRecord(cell);
            cell.Reset(); // set cell to invalid
        }
    }
    if (!cell.IsValid()) {
        cell.flow = flow;
        cell.startTime = now.count();
    }

    cell.endTime = now.count();
    cell.pktCnt += 1;
    cell.byteCnt += byteCnt;
    
    if (flush) {
        OutputRecord(cell);
    }
}


void FlowAggr::HandlePacket(Ptr<Packet> pkt) {
    FlowTuple flow;
    PppHeader pppHeader;
    Ipv4Header ipHdr;
    TcpHeader tcpHdr;
    
    pkt->RemoveHeader(pppHeader);
    if (pppHeader.GetProtocol() != 0x0021) {
        // do not care non ipv4 packet
        pkt->AddHeader(pppHeader);
        return;
    }

    pkt->RemoveHeader(ipHdr);
    flow.srcAddr = ipHdr.GetSource().Get();
    flow.dstAddr = ipHdr.GetDestination().Get();
    flow.proto = ipHdr.GetProtocol();
    if (ipHdr.GetProtocol() == TcpL4Protocol::PROT_NUMBER) {
        pkt->RemoveHeader(tcpHdr);
        flow.srcPort = tcpHdr.GetSourcePort();
        flow.dstPort = tcpHdr.GetDestinationPort();
        constexpr uint8_t shouldFlushMask = TcpHeader::FIN | TcpHeader::RST;
        uint8_t flags = tcpHdr.GetFlags();
        bool shouldFlush = ((flags & shouldFlushMask) != 0);
        DoRecord(flow, pkt->GetSize(), shouldFlush);
        if ((flags & TcpHeader::FIN) == 0) {
            m_activeFlowSet.insert(flow);
        }

        auto ts = Now().GetNanoSeconds();
        if (flags & TcpHeader::SYN) {
            if (!m_concurrentFlowSet.count(flow)) {
                m_totalFlowCnt++;
                m_concurrentFlowSet.insert(flow);
                m_concurrentFlowCntStats.push_back({ts, m_concurrentFlowSet.size()});
            }
        } else if ((flags & TcpHeader::FIN) && m_concurrentFlowSet.count(flow)) {
            m_concurrentFlowSet.erase(flow);
            m_concurrentFlowCntStats.push_back({ts, m_concurrentFlowSet.size()});
        }

        pkt->AddHeader(tcpHdr);
    }
    pkt->AddHeader(ipHdr);
    pkt->AddHeader(pppHeader);
}

void FlowAggr::PrintFlowDurationStats() const {
    if (m_concurrentFlowCntStats.size() == 0) {
        std::cout << "No record" << std::endl;
        return;
    }
    if (m_concurrentFlowSet.size() != 0) {
        std::cout << "!!! ActiveFlowSet is not empty (size=" << m_concurrentFlowSet.size() << std::endl;
        return;
    }
    auto totalDuration = m_concurrentFlowCntStats.back().first - m_concurrentFlowCntStats.front().first;
    std::cout << "total duration: " << totalDuration << std::endl;
    std::cout << "total flow count: " << m_totalFlowCnt << std::endl;

    int64_t maxConcurrentFlowCnt = 0;
    for (auto [_, flowCnt] : m_concurrentFlowCntStats) {
        maxConcurrentFlowCnt = std::max(flowCnt, maxConcurrentFlowCnt);
    }
    std::cout << "maxConcurrentFlowCnt: " << maxConcurrentFlowCnt << std::endl;

    // index: concurrentFlowCnt,  value: duration
    std::vector<int64_t> durationStats;
    durationStats.resize(maxConcurrentFlowCnt + 1, 0);
    for (int i = 0; i < (int)m_concurrentFlowCntStats.size() - 1; i++) {
        auto [ts, flowCnt] = m_concurrentFlowCntStats[i];
        auto duration = m_concurrentFlowCntStats[i + 1].first - ts;
        durationStats[flowCnt] += duration;
    }
    // avg
    int64_t accum = 0;
    for (int i = 1; i < (int)durationStats.size(); i++) {
        accum += i * durationStats[i];
    }
    int64_t avgConcurrentFlowCnt = accum / totalDuration;
    std::cout << "average number of concurrent flows: " << avgConcurrentFlowCnt << std::endl;


    if (maxConcurrentFlowCnt < 20) {
        for (int i = 1; i < (int)durationStats.size(); i++) {
            std::cout << "ConcurrentFlowCnt=" << i << "\taccumDuration=" << durationStats[i] << std::endl;
        }
        return;
    }

    // CDF
    int64_t durationAccum = 0;
    std::queue<double> ticks;
    for (int i = 0; i < 20; i++) {
        ticks.push((double)(i + 1) / 20);
    }
    for (int i = 1; i < (int)durationStats.size(); i++) {
        durationAccum += durationStats[i];
        if (durationAccum >= totalDuration * ticks.front()) {
            std::cout << i << "\t" << ticks.front() << std::endl;
            ticks.pop();
        }
    }
}
