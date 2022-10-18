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

    void EnableStats() { m_statsEnabled = true; }
    void PrintStats() const;

private:
    struct Record;

    int m_hashTableSize;
    microseconds m_ttl;
    std::unique_ptr<Record[]> m_hashTable;

    bool m_statsEnabled = false;

    int m_recordCnt = 0;
    int m_expirCnt = 0;
    int m_collisionCnt = 0;

    void OutputRecord(Record &cell);
};


struct FlowTable::Record {
    FlowTuple flow;
    uint32_t startTime = 0;
    uint32_t endTime = 0;
    uint32_t pktCnt = 0;
    uint32_t byteCnt = 0;

    static constexpr int SerializedSize = FlowTuple::SerializedSize + 16;

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
        bufIt.WriteHtonU32(startTime);
        bufIt.WriteHtonU32(endTime);
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
    void PrintStats();

private:
    Time startTime;
    Time epochEndTime;
    std::set<FlowTuple> activeFlowSet;
    std::vector<int> samples;
};
