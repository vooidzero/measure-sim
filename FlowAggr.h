#pragma once
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/tcp-l4-protocol.h"
#include "ns3/udp-l4-protocol.h"
#include <memory>
#include <ratio>
#include <set>
#include <tuple>
#include <vector>
#include "TimeHelper.h"

using namespace ns3;

struct FlowTuple;

class FlowAggr {
public:
    FlowAggr(int hashTableSize = 4096, microseconds ttl = -1us);
    ~FlowAggr() = default;

    void HandlePacket(Ptr<Packet> pkt);

    void enableStats() { m_statsEnabled = true; }

    int GetRecordCnt() const { return m_recordCnt; }
    int GetExpirCnt() const { return m_expirCnt; }
    int GetCollisionCnt() const { return m_collisionCnt; }

    int64_t PollActiveFlowCnt();
    int GetTotalFlowCnt() const { return m_totalFlowCnt; }
    int GetCurrFlowCnt() const { return m_concurrentFlowSet.size(); }

    void PrintFlowDurationStats() const;

private:
    struct Record;

    static constexpr int MAX_PAYLOAD_SIZE = 1500 - 40;
    
    int m_hashTableSize;
    microseconds m_ttl;
    std::unique_ptr<Record[]> m_hashTable;

    bool m_statsEnabled = false;

    int m_recordCnt = 0;
    int m_expirCnt = 0;
    int m_collisionCnt = 0;

    int64_t m_extraPktCnt = 0;
    int64_t m_extraByteCnt = 0;

    /* accurate statistics of the traffic */
    int m_totalFlowCnt = 0;
    std::set<FlowTuple> m_concurrentFlowSet;
    std::vector<std::pair<int64_t, int64_t>> m_concurrentFlowCntStats; // <timeStamp, concurrentFlowCnt>

    void DoRecord(const FlowTuple &flow, int byteCnt, bool flush = false);
    void OutputRecord(const Record &record);
};


struct FlowTuple {
    uint32_t srcAddr;
    uint32_t dstAddr;
    uint16_t srcPort;
    uint16_t dstPort;
    uint8_t proto;

    static constexpr int SerializedSize = 13;

    uint32_t GetHashValue() const {
        return Hash32(reinterpret_cast<const char*>(this), SerializedSize);
    }

    bool operator == (const FlowTuple &another) const {
        return proto == another.proto
            && srcAddr == another.srcAddr && dstAddr == another.dstAddr
            && srcPort == another.srcPort && dstPort == another.dstPort;
    }

    bool operator != (const FlowTuple &another) const { return !(*this == another); }

    /// for use of std::map
    bool operator < (const FlowTuple &b) const {
        auto left = std::tuple{proto, srcAddr, dstAddr, srcPort, dstPort};
        auto right = std::tuple{b.proto, b.srcAddr, b.dstAddr, b.srcPort, b.dstPort};
        return left < right;
    }

    void Serialize(Buffer::Iterator &bufIt) const {
        bufIt.WriteU8 (proto);
        bufIt.WriteHtonU32(srcAddr);
        bufIt.WriteHtonU32(dstAddr);
        bufIt.WriteHtonU16(srcPort);
        bufIt.WriteHtonU16(dstPort);
    }

    void AppendToBuffer(Buffer &buf) const {
        buf.AddAtEnd(SerializedSize);
        auto end = buf.End();
        end.Prev(SerializedSize);
        Serialize(end);
    }
};

inline std::ostream& operator<< (std::ostream &out, const FlowTuple &flow) {
    static std::map<uint8_t, const char*> protStrMap{
        {TcpL4Protocol::PROT_NUMBER, "TCP"},
        {UdpL4Protocol::PROT_NUMBER, "UDP"}
    };
    if (protStrMap.count(flow.proto) != 0) {
        out << '(' << protStrMap[flow.proto] << ')';
    } else {
        out << "(UnknownProto 0x" << std::hex << flow.proto << std::dec << ')';
    }
    out << Ipv4Address{flow.srcAddr} << ':' << flow.srcPort
        << "<>"
        << Ipv4Address{flow.dstAddr} << ':' << flow.dstPort
    ;
    return out;
}



struct FlowAggr::Record {
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
