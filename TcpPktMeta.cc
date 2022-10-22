#include "TcpPktMeta.h"

#include <type_traits>
#include "ns3/ppp-header.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/tcp-header.h"


std::optional<TcpPktMetadata>
TcpPktMetadata::FromPppPkt(Ptr<const Packet> constPkt, ns3::Time timestamp) {
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
    meta.timestamp = nanoseconds{timestamp.GetNanoSeconds()};
    meta.phyPktSize = constPkt->GetSize();
    meta.flow.srcAddr = ipHdr.GetSource().Get();
    meta.flow.dstAddr = ipHdr.GetDestination().Get();
    meta.flow.proto = ipHdr.GetProtocol();
    meta.flow.srcPort = tcpHdr.GetSourcePort();
    meta.flow.dstPort = tcpHdr.GetDestinationPort();
    meta.tcpFlags = tcpHdr.GetFlags();
    meta.payloadSize = pkt->GetSize();
    return meta;
}


template <class UIntType>
void WriteUInt(std::ostream &out, UIntType value) {
    static_assert(std::is_same<UIntType, uint8_t>::value
        || std::is_same<UIntType, uint16_t>::value
        || std::is_same<UIntType, uint32_t>::value
        || std::is_same<UIntType, uint64_t>::value
    );
    out.write(reinterpret_cast<const char*>(&value), sizeof(UIntType));
}

template <class UIntType>
void ReadUInt(std::istream &in, UIntType *pvalue) {
    static_assert(std::is_same<UIntType, uint8_t>::value
        || std::is_same<UIntType, uint16_t>::value
        || std::is_same<UIntType, uint32_t>::value
        || std::is_same<UIntType, uint64_t>::value
    );
    in.read(reinterpret_cast<char*>(pvalue), sizeof(UIntType));
}


using MagicNumberType = uint16_t;
static constexpr MagicNumberType MagicNumber = 0x7777;

void TcpPktMetadata::WriteToFstream(std::ostream &out) {
    WriteUInt(out, MagicNumber);
    WriteUInt(out, (uint64_t)timestamp.count());
    WriteUInt(out, phyPktSize);
    WriteUInt(out, flow.srcAddr);
    WriteUInt(out, flow.dstAddr);
    WriteUInt(out, flow.srcPort);
    WriteUInt(out, flow.dstPort);
    WriteUInt(out, flow.proto);
    WriteUInt(out, tcpFlags);
    WriteUInt(out, payloadSize);
}

std::optional<TcpPktMetadata>
TcpPktMetadata::FromFstream(std::istream &in) {
    TcpPktMetadata pktMeta;
    uint64_t timestamp;

    MagicNumberType magic = 0;
    ReadUInt(in, &magic);
    if (magic != MagicNumber) {
        return {};
    }

    ReadUInt(in, &timestamp);
    pktMeta.timestamp = decltype(pktMeta.timestamp){(int64_t)timestamp};
    ReadUInt(in, &pktMeta.phyPktSize);
    ReadUInt(in, &pktMeta.flow.srcAddr);
    ReadUInt(in, &pktMeta.flow.dstAddr);
    ReadUInt(in, &pktMeta.flow.srcPort);
    ReadUInt(in, &pktMeta.flow.dstPort);
    ReadUInt(in, &pktMeta.flow.proto);
    ReadUInt(in, &pktMeta.tcpFlags);
    ReadUInt(in, &pktMeta.payloadSize);

    return pktMeta;
}
