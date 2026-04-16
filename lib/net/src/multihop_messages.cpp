#include "multihop_messages.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "multihop.h"

#define min(a, b) (a > b ? b : a)

namespace net {

namespace multihop {

int8_t msgtype(uint8_t *buf, size_t sz, MessageType *type) {
    if (sz < sizeof(Header)) {
        return -1;
    }
    Header *hdr = reinterpret_cast<Header *>(buf);
    if (hdr->type == MessageType::OpenCluster) {
        *type = MessageType::OpenCluster;
    } else if (hdr->type == MessageType::Follow) {
        *type = MessageType::Follow;
    } else if (hdr->type == MessageType::Accept) {
        *type = MessageType::Accept;
    } else if (hdr->type == MessageType::CloseCluster) {
        *type = MessageType::CloseCluster;
    } else if (hdr->type == MessageType::SendAcks) {
        *type = MessageType::SendAcks;
    } else if (hdr->type == MessageType::Datagram) {
        *type = MessageType::Datagram;
    } else if (hdr->type == MessageType::Heartbeat) {
        *type = MessageType::Heartbeat;
    } else {
        *type = MessageType::Unknown;
        return -2;
    }
    return 0;
}

size_t Message::size() {
    switch (hdr.type) {
        case MessageType::OpenCluster:
            return sizeof(OpenCluster) + sizeof(Header);
        case MessageType::Follow:
            return sizeof(Follow) + sizeof(Header);
        case MessageType::Accept:
            return sizeof(Accept) + sizeof(Header);
        case MessageType::CloseCluster:
            return sizeof(CloseCluster) + sizeof(Header);
        case MessageType::SendAcks:
            return sizeof(SendAcks) + sizeof(Header);
        case MessageType::Datagram:
            return sizeof(Datagram) + sizeof(Header);
        case MessageType::Heartbeat:
            return sizeof(Heartbeat) + sizeof(Header);
        default:
            return 0;
    }
}

uint32_t RingList::score(Ring node_ring) {
    if (GATEWAY_RING == node_ring) {
        return UINT32_MAX;
    }
    uint32_t sum = 0;
    uint8_t max = this->ring_start + sizeof(this->counts);
    for (size_t i = 0; i < sizeof(this->counts); ++i) {
        uint8_t multiplier = max - i;
        sum += multiplier * this->counts[i];
    }
    return sum;
}

DatagramIterator::DatagramIterator(uint8_t *buf, size_t sz,
                                   uint16_t max_fragment_bytes)
    : part(0), i(0), buf(buf), sz(sz), max_fragment_bytes(max_fragment_bytes), last_msg_sz(0) {
    size_t count = ceil((double)sz / max_fragment_bytes);
    // don't allow counts > U8 max since the header field is a u8
    // also we will literally never send anything that big
    if (count > UINT8_MAX) {
        this->i = sz;
        this->count = 0;
    } else {
        this->count = count;
    }
}

uint16_t DatagramIterator::rewind() {
    uint16_t last_sz = this->last_msg_sz;
    this->last_msg_sz = 0;
    this->i -= last_sz;
    --this->part;
    return last_sz;
}

bool DatagramIterator::peek(Datagram *d, uint8_t **start, uint8_t **end) {
    if (has_next()) {
        size_t remaining = sz - i;
        uint16_t packet_bytes = min(remaining, max_fragment_bytes);
        d->packet_bytes = packet_bytes;
        d->count = this->count;
        d->part = this->part;
        *start = this->buf + this->i;
        *end = *start + packet_bytes;
        return true;
    } else {
        *start = nullptr;
        *end = nullptr;
        return false;
    }
}

bool DatagramIterator::next(Datagram *d, uint8_t **start, uint8_t **end) {
    bool b = peek(d, start, end);
    if (b) {
        this->last_msg_sz = static_cast<uint16_t>(*end - *start);
        this->part++;
        this->i += static_cast<size_t>(this->last_msg_sz);
    }
    return b;
}

bool DatagramIterator::has_next() { return i != sz; }
}  // namespace multihop
}  // namespace net
