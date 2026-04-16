#pragma once
#include <stddef.h>
#include <stdint.h>

namespace net {

// Node ID
typedef uint8_t Address;

extern const Address GATEWAY_ADDR;
extern const Address UNKNOWN_ADDR;

/**
 * Network protocol identifier.
 */
enum struct Protocol : uint8_t {
    Multihop,
};

struct MacState {
    uint32_t window_start;
    uint32_t window_end;

    MacState(uint32_t start, uint32_t end);
    MacState();
};

struct LinkParams {
    // Maximum size in bytes of a message.
    size_t msg_max_bytes;

    // Buffer to use for buffering incoming/outgoing messages.
    uint8_t *tx_buf;
    size_t tx_sz;

    uint8_t *rx_buf;
    size_t rx_sz;
};

struct LinkState {
    // CDMA code used
    uint8_t code;

    // TX State
    // Currently queued number of bytes.
    size_t tx_bytes_queued;
    // Number of messages queued
    size_t tx_msg_count;

    // RX state
    // Current location of the message being read out from RX buffer
    // This signifies whether there is anything in the RX buf or not.
    uint8_t *rx_cursor;
    // Current message number we are on
    size_t rx_msg_num;

    LinkState();
};

struct NodeParams {
    // Unique node address.
    Address address;
    // This node's time in seconds from Unix epoch (January 1st 1970).
    uint32_t time_s;
};

struct MacParams {
    // Must be >= 1 as the gateway always gets slot 0.
    uint32_t superframe_slot_s;
    // Seconds between intervals
    uint32_t superframe_frequency_s;
    // Must be >= 1.
    uint8_t superframe_slot_count;
    // Number of seconds padded between slots.
    uint8_t guard_period_s;
    // Number of distinct VTDMA windows for spatial reuse.
    // Rings map to windows via: window = ring % reuse_distance.
    uint8_t reuse_distance;

    uint32_t slot_duration();
    uint32_t window_length();
};

}  // namespace net
