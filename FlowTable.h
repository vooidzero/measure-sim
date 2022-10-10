#pragma once
#include "ns3/packet.h"
#include <iterator>
#include <memory>
#include <set>
#include <vector>
#include "FlowTuple.h"
#include "TimeHelper.h"

using namespace ns3;

struct TcpPktMetadata {
    FlowTuple flow;
    uint32_t payloadSize;
    uint8_t tcpFlags;

    static std::optional<TcpPktMetadata> FromPppPkt(Ptr<const Packet>);
};


class FlowTable {
public:
    FlowTable(int hashTableSize = 4096, microseconds ttl = -1us);
    ~FlowTable() = default;

    void DoRecord(const TcpPktMetadata &pktMeta);

    void enableStats() { m_statsEnabled = true; }

    int GetRecordCnt() const { return m_recordCnt; }
    int GetExpirCnt() const { return m_expirCnt; }
    int GetCollisionCnt() const { return m_collisionCnt; }

private:
    struct Record;

    int m_hashTableSize;
    microseconds m_ttl;
    std::unique_ptr<Record[]> m_hashTable;

    bool m_statsEnabled = false;

    int m_recordCnt = 0;
    int m_expirCnt = 0;
    int m_collisionCnt = 0;

    void OutputRecord(const Record &record);
};


struct FlowTable::Record {
    FlowTuple flow;
    uint32_t startTime = 0;
    uint16_t endTime = 0;
    uint32_t pktCnt = 0;
    uint32_t byteCnt = 0;

    static constexpr int SerializedSize = FlowTuple::SerializedSize + 12;

    Record() { flow.proto = 0; }

    bool IsValid() const {
        return flow.proto != 0;
    }

    void Reset() {
        flow.proto = 0;
        pktCnt = 0;
        byteCnt = 0;
    }

    void Serialize(Buffer::Iterator &bufIt) const {
        flow.Serialize(bufIt);
        bufIt.WriteHtonU16(startTime);
        bufIt.WriteHtonU16(endTime);
        bufIt.WriteHtonU32(pktCnt);
        bufIt.WriteHtonU32(byteCnt);
    }

    void AppendToBuffer(Buffer &buf) const {
        buf.AddAtEnd(SerializedSize);
        auto end = buf.End();
        end.Prev(SerializedSize);
        Serialize(end);
    }
};


class FlowStats {
public:
    static constexpr auto EpochDuration = 10ms;

    FlowStats() = default;
    void setStatsStartTime(Time start) {
        startTime = start;
        epochEndTime = startTime + toNsTime(EpochDuration);
    }

    void Record(const TcpPktMetadata &pktMeta);
    void printStats();

private:
    Time startTime;
    Time epochEndTime;
    std::set<FlowTuple> activeFlowSet;
    std::vector<int> samples;
};
