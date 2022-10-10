#pragma once
#include "ns3/nstime.h"
#include <chrono>

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::microseconds;
using std::chrono::nanoseconds;
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
using std::literals::chrono_literals::operator""us;
using std::literals::chrono_literals::operator""ns;

inline auto toNsTime(seconds t)      { return ns3::Seconds(t.count()); }
inline auto toNsTime(milliseconds t) { return ns3::MilliSeconds(t.count()); }
inline auto toNsTime(microseconds t) { return ns3::MicroSeconds(t.count()); }
inline auto toNsTime(nanoseconds t)  { return ns3::NanoSeconds(t.count()); }

inline std::ostream& operator<<(std::ostream &os, microseconds t) {
    os << t.count() << "us";
    return os;
}

inline std::ostream& operator<<(std::ostream &os, milliseconds t) {
    os << t.count() << "ms";
    return os;
}