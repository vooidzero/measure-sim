#pragma once

#include <optional>
#include "ns3/packet.h"
#include "FlowTuple.h"
#include "TimeHelper.h"

struct TcpPktMetadata {
    nanoseconds timestamp;
    uint32_t phyPktSize;

    FlowTuple flow;
    uint8_t tcpFlags;
    uint32_t payloadSize;


    static std::optional<TcpPktMetadata> FromPppPkt(Ptr<const Packet> pkt, ns3::Time timestamp);

    void WriteToFstream(std::ostream &out);

    static std::optional<TcpPktMetadata> FromFstream(std::istream &in);
};
