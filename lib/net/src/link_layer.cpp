#include "link_layer.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "LoraRadio.h"
#include "arduino_stubs.h"
#include "net.h"
#include "time_helpers.h"

namespace net {
namespace link {

const uint8_t DEFAULT_CODE = 0;

// Get the size of a specific member in struct
#define sizeofmem(type, member) (sizeof(((type *)0)->member))

// Module state
static bool INITIALIZED = false;
static NodeParams NODE;
static MacParams MAC;
static MacState MAC_STATE;
static LinkParams LINK;
static LinkState LINK_STATE;

// All timing helpers take an explicit `now` to avoid multiple independent
// reads of time::now() within a single logical operation. In the nexus
// simulator, each read of ctl.time.s is a FUSE request that can be
// interleaved with other nodes' requests, so consecutive reads may return
// different values.

static inline uint32_t next_time_interval(uint32_t now, uint32_t interval);
static inline bool after_window(uint32_t now);
static inline bool in_active_window(uint32_t now);
static inline int16_t current_slot(uint32_t now);
static inline size_t time_left_in_slot(uint32_t now);

// Helper functions for writing out messages
static inline uint8_t *write_head();
// Helper functions for creating frame headers
static bool fill_frame_header();
static inline bool header_checksum(FrameHeader *hdr, uint8_t *buf, size_t sz);
static inline uint16_t calculate_checksum(uint8_t *start, uint8_t *end);

MessageVec::MessageVec(const uint8_t *buf, size_t sz, MessageVec *next)
    : buf(buf), sz(sz), next(next) {}

void MessageVec::set_next(MessageVec *next) { this->next = next; }

size_t MessageVec::size() {
    MessageVec *p = this;
    size_t sz = 0;
    while (nullptr != p) {
        sz += p->sz;
        p = p->next;
    }
    return sz;
}

void MessageVec::write(uint8_t *buf) {
    MessageVec *p = this;
    size_t i = 0;
    while (nullptr != p) {
        memcpy(buf + i, p->buf, p->sz);
        i += p->sz;
        p = p->next;
    }
}

uint32_t next_time_interval(uint32_t now, uint32_t interval) {
    if (interval == 0) {
        return 0;
    }
    uint32_t remainder = now % interval;
    if (remainder == 0) {
        return now;
    }
    return now - remainder + interval;
}

// Maximum number of bytes available for a new message.
size_t tx_bytes_available() {
    size_t hdr_sz = sizeof(FrameHeader);
    // 1-byte length prefixes before each message
    size_t metadata_bytes = LINK_STATE.tx_msg_count;
    size_t bytes_used = hdr_sz + metadata_bytes + LINK_STATE.tx_bytes_queued;
    if (bytes_used > LINK.tx_sz) {
        return 0;
    }
    return LINK.tx_sz - bytes_used;
}

// Next available byte to start writing a message and its length prefix at
uint8_t *write_head() {
    size_t hdr_sz = sizeof(FrameHeader);
    return LINK.tx_buf + hdr_sz + LINK_STATE.tx_msg_count +
           LINK_STATE.tx_bytes_queued;
}

int16_t current_slot(uint32_t now) {
    if (after_window(now)) {
        uint32_t window_start =
            next_time_interval(now, MAC.superframe_frequency_s);
        MAC_STATE = MacState(window_start, window_start + MAC.window_length());
    }
    if (!in_active_window(now)) {
        return -1;
    }
    uint32_t slot = (now - MAC_STATE.window_start) / MAC.slot_duration();
    return static_cast<int16_t>(slot);
}

size_t time_left_in_slot(uint32_t now) {
    uint32_t full_slot = MAC.slot_duration();
    uint32_t elapsed = now - MAC_STATE.window_start;
    return full_slot - (elapsed % full_slot);
}

uint16_t calculate_checksum(uint8_t *start, uint8_t *end) {
    uint16_t checksum = 0;
    for (uint8_t *p = start; p < end; ++p) {
        checksum += *p;
    }
    return checksum;
}

bool header_checksum(FrameHeader *hdr, uint8_t *buf, size_t sz) {
    uint8_t *start = buf + offsetof(FrameHeader, checksum) +
                     sizeofmem(FrameHeader, checksum);
    uint8_t *end = buf + sizeof(FrameHeader) + hdr->payload_bytes;
    if (end > buf + sz) {
        return false;
    }
    hdr->checksum = calculate_checksum(start, end);
    return true;
}

bool fill_frame_header() {
    FrameHeader *hdr = reinterpret_cast<FrameHeader *>(LINK.tx_buf);
    memset(hdr, 0, sizeof(FrameHeader));
    hdr->payload_bytes = LINK_STATE.tx_msg_count + LINK_STATE.tx_bytes_queued;
    hdr->sender_time = time::now();
    hdr->sender_address = NODE.address;
    hdr->message_count = LINK_STATE.tx_msg_count;
    if (!header_checksum(hdr, LINK.tx_buf, LINK.tx_sz)) {
        return false;
    }
    return true;
}

bool after_window(uint32_t now) { return now >= MAC_STATE.window_end; }

bool in_active_window(uint32_t now) {
    return now >= MAC_STATE.window_start && now < MAC_STATE.window_end;
}

RC init(NodeParams *node, MacParams *mac, LinkParams *link) {
    if (mac->superframe_slot_count < 1) {
        return RC::InvalidSlotCount;
    } else if (mac->superframe_slot_s < 1) {
        return RC::InvalidSlotLength;
    }

    NODE = *node;
    MAC = *mac;
    LINK = *link;
    LINK_STATE = LinkState();

    // Initialize window using a single time read
    uint32_t now = time::now();
    if (after_window(now)) {
        uint32_t window_start =
            next_time_interval(now, MAC.superframe_frequency_s);
        MAC_STATE = MacState(window_start, window_start + MAC.window_length());
    }

    INITIALIZED = true;
    return RC::Ok;
}

RC recv(uint32_t timeout_ms) {
    if (!INITIALIZED) {
        return RC::NotInit;
    }
    uint32_t now = time::now();
    // Update window and check if active using the same time value
    if (after_window(now)) {
        uint32_t window_start =
            next_time_interval(now, MAC.superframe_frequency_s);
        MAC_STATE = MacState(window_start, window_start + MAC.window_length());
    }
    if (!in_active_window(now)) {
        return RC::NotInWindow;
    }

    truncate_rx();
    // Receive from medium, make sure the message is minimally the size of a
    // frame header
    uint8_t len = LINK.rx_sz;
    lora::RC rc = lora::wait_recv(LINK.rx_buf, len, timeout_ms);
    if (rc == lora::RC::TimedOut) {
        return RC::TimedOut;
    } else if (rc != lora::RC::Okay) {
        return RC::PhyError;
    } else if (len <= sizeof(FrameHeader)) {
        return RC::InvalidMsg;
    }

    // Validate the frame header and data
    FrameHeader hdr;
    memcpy(&hdr, LINK.rx_buf, sizeof(FrameHeader));
    uint16_t received_checksum = hdr.checksum;
    size_t total_len = sizeof(FrameHeader) + hdr.payload_bytes;
    if (total_len != len) {
        return RC::MsgSizeMismatch;
    } else if (total_len > LINK.rx_sz) {
        return RC::BufSize;
    }
    header_checksum(&hdr, LINK.rx_buf, LINK.rx_sz);
    if (hdr.checksum != received_checksum) {
        return RC::ChecksumError;
    }

    rewind();
    return RC::Ok;
}

RC getmsg(uint8_t **buf, size_t *sz) {
    if (LINK_STATE.rx_cursor == nullptr) {
        return RC::NoMsg;
    }
    FrameHeader *hdr = reinterpret_cast<FrameHeader *>(LINK.rx_buf);
    if (LINK_STATE.rx_msg_num >= hdr->message_count) {
        return RC::NoMsg;
    }
    uint8_t len = *LINK_STATE.rx_cursor;
    uint8_t *frame_end = LINK.rx_buf + sizeof(FrameHeader) + hdr->payload_bytes;
    if (LINK_STATE.rx_cursor + len + 1 > frame_end) {
        return RC::OutOfBounds;
    }
    *buf = LINK_STATE.rx_cursor + 1;
    *sz = len;
    LINK_STATE.rx_cursor += len + 1;
    LINK_STATE.rx_msg_num += 1;
    return RC::Ok;
}

RC sendmsg(MessageVec *v) {
    if (!INITIALIZED) {
        return RC::NotInit;
    }
    size_t sz = v->size();
    if (tx_bytes_available() < sz + 1ul) {
        return RC::BufSize;
    } else if (sz > LINK.msg_max_bytes) {
        return RC::MsgTooBig;
    }
    uint8_t *w = write_head();
    *w = sz;
    v->write(++w);
    LINK_STATE.tx_bytes_queued += sz;
    ++LINK_STATE.tx_msg_count;
    return RC::Ok;
}

RC sendmsg(const uint8_t *buf, uint8_t sz) {
    MessageVec v(buf, sz);
    return sendmsg(&v);
}

RC flush() {
    if (!INITIALIZED) {
        return RC::NotInit;
    }
    uint32_t now = time::now();
    if (after_window(now)) {
        uint32_t window_start =
            next_time_interval(now, MAC.superframe_frequency_s);
        MAC_STATE = MacState(window_start, window_start + MAC.window_length());
    }
    if (!in_active_window(now)) {
        return RC::NotInWindow;
    } else if (!fill_frame_header()) {
        return RC::HeaderError;
    }

    size_t len_bytes = sizeof(FrameHeader) + LINK_STATE.tx_msg_count +
                       LINK_STATE.tx_bytes_queued;
    {
        lora::RC rc = lora::send(LINK.tx_buf, len_bytes);
        if (rc != lora::RC::Okay) {
            return RC::FlushFailed;
        }
    }
    truncate_tx();
    return RC::Ok;
}

int16_t active_slot() { return current_slot(time::now()); }

/**
 * Helper function that gets both the epoch and offset time for a particular
 * slot. Other time-keeping functions are built off this.
 */
RC epoch_and_offset(uint8_t n, uint32_t *epoch, uint32_t *offset) {
    if (!INITIALIZED) {
        return RC::NotInit;
    } else if (n >= MAC.superframe_slot_count) {
        return RC::InvalidSlot;
    }
    uint32_t now = time::now();
    int32_t current = current_slot(now);
    // If we aren't currently in a window or if the slot is behind `current`,
    // we will need to wait for the next interval.
    if (current < 0 || n < static_cast<size_t>(current)) {
        uint32_t window_start =
            next_time_interval(now, MAC.superframe_frequency_s);
        *offset = window_start - now + n * MAC.slot_duration();
    } else if (static_cast<size_t>(current) <= n) {
        size_t slots_left = n - static_cast<size_t>(current);
        if (0 == slots_left) {
            // Already in the target slot; act immediately.
            *offset = 0;
        } else {
            // Wait for remaining time in current slot plus any full slots.
            *offset = time_left_in_slot(now);
            if (slots_left > 1) {
                *offset += (slots_left - 1) * MAC.slot_duration();
            }
        }
    }
    *epoch = now + *offset;
    return (current < 0) ? RC::NotInWindow : RC::Ok;
}

RC slot_time(uint8_t n, uint32_t *epoch) {
    uint32_t offset;
    return epoch_and_offset(n, epoch, &offset);
}

RC seconds_until_self(uint32_t *offset) {
    uint32_t epoch;
    return epoch_and_offset(NODE.address, &epoch, offset);
}

RC seconds_until_slot(uint8_t n, uint32_t *offset) {
    uint32_t epoch;
    return epoch_and_offset(n, &epoch, offset);
}
void truncate_tx() {
    LINK_STATE.tx_msg_count = 0;
    LINK_STATE.tx_bytes_queued = 0;
}

void truncate_rx() {
    LINK_STATE.rx_cursor = nullptr;
    LINK_STATE.rx_msg_num = 0;
}

void rewind() {
    LINK_STATE.rx_cursor = LINK.rx_buf + sizeof(FrameHeader);
    LINK_STATE.rx_msg_num = 0;
}

size_t mtu() { return LINK.msg_max_bytes; }

uint8_t hash_cdma_code(uint8_t id) {
    uint8_t h = id % 7;
    return h + 6;
}

uint8_t get_cdma_code() { return LINK_STATE.code; }

void set_cdma_code(uint8_t id) {
    uint8_t code = hash_cdma_code(id);
    LINK_STATE.code = code;
    lora::set_spreading_factor(static_cast<lora::SF>(code));
}

}  // namespace link
}  // namespace net
