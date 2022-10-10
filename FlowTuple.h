#pragma once

#include <tuple>
#include "ns3/tcp-l4-protocol.h"
#include "ns3/udp-l4-protocol.h"
using namespace ns3;

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
