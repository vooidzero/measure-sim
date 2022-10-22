#include "MultiLevelTable.h"

#include "ns3/tcp-header.h"
#include "TcpPktMeta.h"


static Hasher murmur3{Create<Hash::Function::Murmur3>()};
static Hasher fnv1a{Create<Hash::Function::Fnv1a>()};
std::vector<uint32_t> GetHashs(const FlowTuple &tuple, int mod) {
    std::vector<uint32_t> ret {
        (uint32_t) murmur3.clear().GetHash32(reinterpret_cast<const char*>(&tuple), 13),
        (uint32_t) murmur3.clear().GetHash64(reinterpret_cast<const char*>(&tuple), 13),
        (uint32_t) fnv1a.clear().GetHash32(reinterpret_cast<const char*>(&tuple), 13),
        (uint32_t) fnv1a.clear().GetHash64(reinterpret_cast<const char*>(&tuple), 13)
    };
    for (auto &x : ret) {
        x %= mod;
    }
    return ret;
}

MultiLevelTable::MultiLevelTable(const Config &cfg)
    : m_cfg{cfg}, m_table(new Cell[cfg.rowCnt * cfg.colCnt])
{
    m_random = CreateObject<UniformRandomVariable> ();
    m_random->SetStream(1);
}

MultiLevelTable::Cell& MultiLevelTable::CellAt(int row, int col) {
    return m_table[row * m_cfg.colCnt + col];
}

void MultiLevelTable::OutputRecord(Cell &cell) {
    cell.Reset();
    if (m_statsEnabled) {
        m_outputRecordCnt++;
    }
}

void MultiLevelTable::DoRecord(const TcpPktMetadata &pktMeta) {
    nanoseconds now = pktMeta.timestamp;
    if (now >= m_statsBeginTs) {
        m_statsEnabled = true;
    }

    const FlowTuple &flow = pktMeta.flow;
    constexpr uint8_t FlushMask = TcpHeader::FIN | TcpHeader::RST;
    bool shouldFlush = ((pktMeta.tcpFlags & FlushMask) != 0);
    auto hashs = GetHashs(flow, m_cfg.rowCnt);
    if (!m_cfg.diffHashFunc) {
        for (int i = 1; i < (int)hashs.size(); i++) {
            hashs[i] = hashs[0];
        }
    }

    auto getCell = [&hashs, this](int col) -> Cell& {
        return CellAt(hashs[col], col);
    };

    for (int col = 0; col < m_cfg.colCnt; col++) {
        Cell &cell = getCell(col);
        if ((!cell.IsValid()) || flow != cell.flow) {
            continue;
        }
        if (shouldFlush) {
            OutputRecord(cell);
            return;
        }
        decltype(now) startTime{cell.startTime};
        bool isExpired = (m_cfg.ttl > 0us && now - startTime > m_cfg.ttl);
        if (isExpired) {
            if (m_statsEnabled) m_expirCnt++;
            OutputRecord(cell);
            cell.flow = flow;
            cell.startTime = now.count();
        } else if (m_cfg.alpha >= 0) {
            uint32_t sample = now.count() - cell.endTime;
            cell.updateInterval = m_cfg.alpha * sample
                                + (1 - m_cfg.alpha) * cell.updateInterval;
        }
        cell.endTime = now.count();
        cell.pktCnt += 1;
        cell.byteCnt += pktMeta.payloadSize;
        return;
    }

    if (shouldFlush) {
        if (m_statsEnabled) {
            m_outputRecordCnt++;
        }
        return;
    }
    
    int colToInsert = -1;
    for (int col = 0; col < m_cfg.colCnt; col++) {
        if (!getCell(col).IsValid()) {
            colToInsert = col;
            break;
        }
    }
    if (colToInsert < 0) {
        if (m_cfg.alpha < 0) {
            // using random replace policy
            colToInsert = m_random->GetInteger(0, m_cfg.colCnt - 1);
        } else {
            colToInsert = 0;
            uint32_t maxUpdateInterval = 0;
            getCell(0).updateInterval;
            for (int col = 0; col < m_cfg.colCnt; col++) {
                Cell &cell = getCell(col);
                uint32_t t = now.count() - cell.endTime;
                uint32_t updateInterval = m_cfg.alpha * t + (1 - m_cfg.alpha) * cell.updateInterval;
                if (updateInterval > maxUpdateInterval) {
                    maxUpdateInterval = updateInterval;
                    colToInsert = col;
                }
            }
        }
        if (m_statsEnabled) m_castoutCnt++;
        OutputRecord(getCell(colToInsert));
    }

    Cell &cell = getCell(colToInsert);
    cell.flow = flow;
    cell.startTime = now.count();
    cell.endTime = now.count();
    cell.pktCnt += 1;
    cell.byteCnt += pktMeta.payloadSize;
}

void MultiLevelTable::PrintStats() const {
    std::cout << "======== replacePolicy=";
    if (m_cfg.alpha < 0) {
        std::cout << "random";
    } else {
        std::cout << "alpha=" << m_cfg.alpha;
    }
    std::cout << ", diffHash=" << (m_cfg.diffHashFunc ? "true" : "false")
            << ", rowCnt=" << m_cfg.rowCnt
            << ", colCnt=" << m_cfg.colCnt
            << ", ttl=" << m_cfg.ttl
            << " ========"
            << std::endl;
    std::cout << "records=" << m_outputRecordCnt
            << ", expirs=" << m_expirCnt
            << ", castOut=" << m_castoutCnt
            << std::endl;
    std::cout << std::endl << std::endl;
}