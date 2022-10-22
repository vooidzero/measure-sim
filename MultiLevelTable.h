#pragma once
#include "ns3/core-module.h"
#include "FlowTuple.h"
#include "TimeHelper.h"

struct TcpPktMetadata;

class MultiLevelTable {
public:
    struct Config {
        int rowCnt; // using hash value as index to find which row each flow belong to
        int colCnt; // how many cells for each hash value
        microseconds ttl;
        double alpha; // if negative, replace policy is random
        bool diffHashFunc;
    };

    MultiLevelTable(const Config &config);
    
    void DoRecord(const TcpPktMetadata &pktMeta);
    
    void SetStatsBeginTs(nanoseconds ts) {
        m_statsEnabled = false;
        m_statsBeginTs = ts;
    }
    void PrintStats() const;

private:
    struct Cell;

    const Config m_cfg;
    std::unique_ptr<Cell[]> m_table;
    Ptr<UniformRandomVariable> m_random;

    nanoseconds m_statsBeginTs{0};
    bool m_statsEnabled = false;
    int m_outputRecordCnt = 0;
    int m_expirCnt = 0;
    int m_castoutCnt = 0;

    Cell& CellAt(int row, int col);
    void OutputRecord(Cell &cell);
};


struct MultiLevelTable::Cell {
    FlowTuple flow;
    uint32_t startTime = 0;
    uint32_t endTime = 0;
    uint32_t pktCnt = 0;
    uint32_t byteCnt = 0;

    uint32_t updateInterval = 0;

    Cell() { flow.proto = 0; }

    bool IsValid() const {
        return flow.proto != 0;
    }

    void Reset() {
        flow.proto = 0;
        pktCnt = 0;
        byteCnt = 0;
        updateInterval = 0;
    }
};