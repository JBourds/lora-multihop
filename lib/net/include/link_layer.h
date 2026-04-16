/**
 * link_layer.h
 *
 * TDMA link/MAC layer which ensures transmissions only occur on the scheduled
 * period for the sending node (slot corresponding to its address).
 *
 * `sendmsg` and `recvmsg` are the interfaces for
 * messages to enter/egress the MAC layer. All other functions are designed for
 * use by higher layers to coordinate when messages get sent.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net.h"

namespace net {
namespace link {

extern const uint8_t DEFAULT_CODE;

enum struct RC : uint8_t {
    Ok,
    NotInit,
    InvalidDutyCycle,
    InvalidSlotCount,
    InvalidSlotLength,
    BufSize,
    PhyError,
    InvalidSlot,
    FlushFailed,
    InvalidMsg,
    MsgSizeMismatch,
    NoMsg,
    MsgTooBig,
    ChecksumError,
    OutOfBounds,
    HeaderError,
    NodeAddressBounds,
    NotYourTurn,
    NotInWindow,
    TimedOut,
};

struct FrameHeader {
    // Checksum performed over every byte following it
    uint16_t checksum;
    // Total number of bytes in transmission not counting header
    uint16_t payload_bytes;
    // Current time of the sender.
    uint32_t sender_time;
    // ID of the sending node.
    Address sender_address;
    // Number of messages in this frame.
    uint8_t message_count;
    // Unused but explicit for padding.
    uint8_t _pad[2];
};

struct MessageVec {
    const uint8_t *buf;
    size_t sz;
    MessageVec *next;

    size_t size();
    void write(uint8_t *buf);
    void set_next(MessageVec *next);

    MessageVec(const uint8_t *buf, size_t sz, MessageVec *next = nullptr);
};

/**
 * Initialize the TDMA protocol with `node`, `tdma`, and `link` params.
 */
RC init(NodeParams *node, MacParams *mac, LinkParams *link);

/**
 * Follow linked list of message fragments to include in a single message.
 */
RC sendmsg(MessageVec *msg);

/**
 * Queue a message to be sent. This is a delivery from higher layer to lower
 * layer. Does not get immediately sent unless it is this node's window.
 */
RC sendmsg(const uint8_t *buf, uint8_t sz);

/**
 * Get a message from the RX buffer if there is one. Fills the `buf` and `sz`
 * arguments if there is one. Once a message has been received, it cannot be
 * received again unless rewinded.
 */
RC getmsg(uint8_t **buf, size_t *sz);

/**
 * Attempt to receive a message, waiting up to `timeout_ms` milliseconds.
 * If `timeout_ms` is 0, receive without blocking.
 */
RC recv(uint32_t timeout_ms);

/**
 * Get the currently active slot, or -1 if there is no active frame.
 */
int16_t active_slot();

/**
 * Get the epoch time and offset in seconds for when a slot occurs.
 *
 * @param n: Slot number.
 * @param epoch: Out-parameter with the absolute epoch time.
 * @param offset: Offset in seconds until the slot.
 */
RC epoch_and_offset(uint8_t n, uint32_t *epoch, uint32_t *offset);

/**
 * Get the epoch time in seconds for the next time a slot will occur.
 *
 * @param n: Slot number.
 * @param offset: Out-parameter with the epoch time when slot `n` starts.
 */
RC slot_time(uint8_t n, uint32_t *epoch);

/**
 * Get the number of seconds until next instance of slot `n`. If greater than
 * `superframe_slot_count`, returns a failure return code and 0 in `seconds`.
 * Otherwise, return the value in `seconds`.
 *
 * @param n: Slot number.
 * @param offset: Out-parameter with the number of seconds until the slot.
 */
RC seconds_until_slot(uint8_t n, uint32_t *offset);

/**
 * Same behavior as `seconds_until_slot` but use the current node's address as
 * an argument for `n`.
 */
RC seconds_until_self(uint32_t *offset);

/**
 * Flush outgoing messages, actually delivering them.
 */
RC flush();

/**
 * Truncate outgoing messages.
 */
void truncate_tx();

/**
 * Truncate received messages.
 */
void truncate_rx();

/**
 * Rewind to the first message (if there is any) in the RX buffer.
 */
void rewind();

/**
 * @returns (size_t): Maximum transmissible unit.
 */
size_t mtu();

/**
 * @returns (size_t): Number of bytes left in the TX buffer.
 */
size_t tx_bytes_available();

/**
 * Map IDs to one of the supported CDMA codes (LoRa spreading factors are 6-12).
 */
uint8_t hash_cdma_code(uint8_t id);

/**
 * Get current CDMA code being used.
 */
uint8_t get_cdma_code();

/**
 * Set CDMA code in physical layer.
 */
void set_cdma_code(uint8_t id);
}  // namespace link
}  // namespace net
