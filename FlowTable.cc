#include "FlowTable.h"

#include <algorithm>
#include "ns3/simulator.h"
#include "ns3/tcp-header.h"
#include "TcpPktMeta.h"

NS_LOG_COMPONENT_DEFINE ("FlowTable");

FlowTable::FlowTable(int hashTableSize, microseconds ttl)
    : m_hashTableSize{hashTableSize},
    m_ttl{ttl},
    m_hashTable{new Record[hashTableSize]}
{}

void FlowTable::OutputRecord(Record &cell) {
    if (m_statsEnabled) {
        m_recordCnt++;
    }
    cell.Reset();
}

void FlowTable::DoRecord(const TcpPktMetadata &pktMeta) {
    nanoseconds now = pktMeta.timestamp;
    if (now >= m_statsBeginTs) {
        m_statsEnabled = true;
    }
    
    const FlowTuple &flow = pktMeta.flow;
    uint32_t idx = flow.GetHashValue() % m_hashTableSize;
    auto &cell = m_hashTable[idx];

    constexpr uint8_t shouldFlushMask = TcpHeader::FIN | TcpHeader::RST;
    bool shouldFlush = ((pktMeta.tcpFlags & shouldFlushMask) != 0);

    if (cell.IsValid()) {
        if (shouldFlush) {
            if (flow == cell.flow) {
                OutputRecord(cell);
            } else if (m_statsEnabled) {
                m_recordCnt++;
            }
            return;
        }

        decltype(now) startTime{cell.startTime};
        bool isExpired = (m_ttl > 0us && now - startTime > m_ttl);
        if (m_statsEnabled) {
            if (flow != cell.flow) {
                m_collisionCnt++;
            } else if (isExpired) {
                m_expirCnt++;
            }
        }
        
        if (flow != cell.flow || isExpired) {
            OutputRecord(cell); // set cell to invalid
        }
    } else if (shouldFlush) {
        if (m_statsEnabled) {
            m_recordCnt++;
        }
        return;
    }

    if (!cell.IsValid()) {
        cell.flow = flow;
        cell.startTime = now.count();
    }

    cell.endTime = now.count();
    cell.pktCnt += 1;
    cell.byteCnt += pktMeta.payloadSize;
}

void FlowTable::PrintStats() const {
    std::cout << "========"
            << " Table entCnt=" << m_hashTableSize
            << ", ttl=" << m_ttl
            << " ========\n";
    std::cout << "records: " << m_recordCnt
                << ", expires: " << m_expirCnt
                << ", collisions: " << m_collisionCnt
                << std::endl;
    std::cout << std::endl << std::endl;
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

void FlowStats::PrintStats() {
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