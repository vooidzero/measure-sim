#pragma once
#include "ns3/callback.h"

/// @brief Convert lambda or std::funtion to ns3::Callback.
/// @note Make sure that the lifetime of `callable` is longer than ns3::Callback returned
template <class Callable>
auto MakeCallbackFromCallable(Callable &callable) {
    return MakeCallback(&Callable::operator(), &callable);
}

/*
// Example:
int main() {
    auto lambda = [this](Ptr<const Packet>){ ... };
    xxx->TraceConnectWithoutContext(
        "SomeTraceSource",
        MakeCallbackFromCallable(lambda)
    );
}
*/