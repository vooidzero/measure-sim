#include "FlowTable.h"
#include "TimeHelper.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ipv4-address.h"
#include "ns3/ppp-header.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/tcp-header.h"
#include <algorithm>
#include <queue>

NS_LOG_COMPONENT_DEFINE ("FlowTable");

std::optional<TcpPktMetadata> 
TcpPktMetadata::FromPppPkt(Ptr<const Packet> constPkt) {
    PppHeader pppHeader;
    Ipv4Header ipHdr;
    TcpHeader tcpHdr;
    TcpPktMetadata meta;
    auto pkt = constPkt->Copy();
    pkt->RemoveHeader(pppHeader);
    if (pppHeader.GetProtocol() != 0x0021) {
        // non-ipv4 packet
        return {};
    }
    pkt->RemoveHeader(ipHdr);
    if (ipHdr.GetProtocol() != TcpL4Protocol::PROT_NUMBER) {
        return {};
    }
    pkt->RemoveHeader(tcpHdr);
    meta.flow.srcAddr = ipHdr.GetSource().Get();
    meta.flow.dstAddr = ipHdr.GetDestination().Get();
    meta.flow.proto = ipHdr.GetProtocol();
    meta.flow.srcPort = tcpHdr.GetSourcePort();
    meta.flow.dstPort = tcpHdr.GetDestinationPort();
    meta.tcpFlags = tcpHdr.GetFlags();
    meta.payloadSize = pkt->GetSize();
    return meta;
}

FlowTable::FlowTable(int hashTableSize, microseconds ttl)
    : m_hashTableSize{hashTableSize},
    m_ttl{ttl},
    m_hashTable{new Record[hashTableSize]}
{}

void FlowTable::OutputRecord(const Record &record) {
    if (m_statsEnabled) {
        m_recordCnt++;
    }
}

void FlowTable::DoRecord(const TcpPktMetadata &pktMeta) {
    const FlowTuple &flow = pktMeta.flow;
    uint32_t idx = flow.GetHashValue() % m_hashTableSize;
    microseconds now{Now().GetMicroSeconds()};
    auto &cell = m_hashTable[idx];
    if (cell.IsValid()) {
        bool isExpired = (m_ttl > 0us && now - microseconds{cell.startTime} > m_ttl);
        if (m_statsEnabled) {
            if (flow != cell.flow) {
                m_collisionCnt++;
            } else if (isExpired) {
                m_expirCnt++;
            }
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
    cell.byteCnt += pktMeta.payloadSize;
    
    uint8_t shouldFlushMask = TcpHeader::FIN | TcpHeader::RST;
    bool shouldFlush = ((pktMeta.tcpFlags & shouldFlushMask) != 0);
    if (shouldFlush) {
        OutputRecord(cell);
    }
}

void FlowStats::Record(const TcpPktMetadata &pktMeta) {
    auto now = Now();
    if (now < startTime) {
        return;
    }

    if (now >= epochEndTime) {
        samples.push_back(activeFlowSet.size());
        activeFlowSet.clear();
        epochEndTime += toNsTime(EpochDuration);
    }

    activeFlowSet.insert(pktMeta.flow);
}

void FlowStats::printStats() {
    if (Now() > epochEndTime - MicroSeconds(1)) {
        samples.push_back(activeFlowSet.size());
        activeFlowSet.clear();
        epochEndTime += toNsTime(EpochDuration);
    }

    std::cout << "samples of number of concurrent flows in " << EpochDuration << ":";
    for (auto x : samples) {
        std::cout << " " << x;
    }
    std::cout << std::endl;

    int sum = 0;
    for (auto x : samples) {
        sum += x;
    }
    std::cout << "average: " << (double)sum / samples.size() << std::endl;
}