#pragma once
/// @file network_message.h
/// @brief Message envelope that travels through the simulated (or real) network.

#include "common/types.h"
#include "exchange/market_data.h"

namespace njt {

enum class MessageType : uint8_t {
    MarketData,
    OrderSubmission,
    ExecutionReport,
    Heartbeat,
    SnapshotRequest,
    SnapshotResponse
};

/// A message transmitted between the exchange and the trading engine.
///
/// In the discrete-event simulation the `delivery_timestamp` is computed by
/// the LatencyModel at send-time.  In the real-socket path it is recorded on
/// receipt.
struct NetworkMessage {
    SequenceNumber sequence           = 0;
    Timestamp      send_timestamp     = 0;
    Timestamp      delivery_timestamp = 0;  // set by network simulator
    MessageType    type               = MessageType::MarketData;

    MarketUpdate   market_update{};         // primary payload

    bool     is_duplicate     = false;
    bool     is_retransmit    = false;
    uint32_t retransmit_count = 0;
};

} // namespace njt
