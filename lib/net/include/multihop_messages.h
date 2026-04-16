#pragma once
#include "multihop_defs.h"
#include "net.h"

namespace net {
namespace multihop {

enum struct MessageType : uint8_t {
    // During the advertising phase, nodes will announce clusterhead status and
    // receive followers. This sends the clusterhead's ring list.
    OpenCluster,
    // Request to follow a specific clusterhead.
    Follow,
    // Send an acceptance for a request to join a cluster along with the slot #
    // and virtual TDMA window start.
    Accept,
    // Shut down a clusterhead.
    CloseCluster,
    // Acknowledgement sent during slot 0 of the clusterhead's virtual TDMA
    // window containing the latest sequence number received for each slot in
    // its TDMA window.
    SendAcks,
    // Datagram fragment with user-data.
    Datagram,
    // Announcement of a node's presence and their ring
    Heartbeat,
    Unknown,
};

int8_t msgtype(uint8_t *buf, size_t sz, MessageType *type);

#pragma pack(push, 1)
struct Header {
    // unique sequence number (serves as a key with count/part)
    uint32_t sequence;
    // sender's address
    Address src;
    // type of message following the header
    MessageType type;
};
#pragma pack(pop)

struct Heartbeat {
    Ring ring;
};

/**
 * Sent in `OpenCluster` messages.
 * Contains an offset with the lowest ring seen, and how many rings of that
 * level and up to 6 rings above it are (7 is arbitrary here).
 */
struct RingList {
    uint8_t ring_start;
    uint8_t counts[8];

    /**
     * @param node: Ring of the node with this list of neighbors.
     *
     * @returns (uint32_t) Metric used to determine the "best" neighboring
     * cluster to choose.
     */
    uint32_t score(Ring node);
};

/**
 * Announce this node as a new clusterhead.
 */
struct OpenCluster {
    Ring ring;
    Address node;
    RingList neighbors;
};

/**
 * Request to join a clusterhead.
 * Sent from `follower` -> `clusterhead`.
 */
struct Follow {
    Address clusterhead;
    Address follower;
};

/**
 * Accept follow request.
 * Sent from clusterhead -> `follower`
 */
#pragma pack(push, 1)
struct Accept {
    // Indicate whether the follower was actually accepted or not.
    bool accepted;
    // Specify specific follower being accepted.
    Address follower;
    // Offset from global slots to start at.
    uint8_t start_slot;
    // TDMA slot being assigned relative to `start_slot`.
    uint8_t relative_slot;
    // Sending node's time.
    uint32_t remote_time_s;
};
#pragma pack(pop)

/**
 * Announce this node is closing as a clusterhead, or renominate this node to
 * fast-track all its followers to not look for a new clusterhead.
 */
struct CloseCluster {
    Address nomination;
    RingList neighbors;
};

/**
 * Potentially multi-message array containing acks for the last received
 * sequence number for each slot in the virtual TDMA window. For the
 * clusterhead, this contains the gateway's last sent message.
 *
 * This has a similar structure to the `Datagram` header, but has `start` and
 * `end` values to signify which slot numbers are included in this
 * acknowledgement. Forms a half-open range [start, end).
 */
struct SendAcks {
    uint8_t start;
    uint8_t end;
    // ...variable length data
};

/**
 * Datagram transfer header. This prefixes the actual data in the message.
 */
struct Datagram {
    // number of bytes in this fragment
    uint16_t packet_bytes;
    // number of fragments in this datagram
    uint8_t count;
    // `part` / `count` datagram fragments
    uint8_t part;
    // ...variable length data
};

/**
 * Struct used to make disassembling buffer into datagram fragments easier.
 */
struct DatagramIterator {
    uint8_t part;
    uint8_t count;
    size_t i;
    uint8_t *buf;
    size_t sz;
    uint16_t max_fragment_bytes;
    uint16_t last_msg_sz;

    DatagramIterator(uint8_t *buf, size_t sz, uint16_t max_fragment_bytes);
    bool next(Datagram *d, uint8_t **start, uint8_t **end);
    bool peek(Datagram *d, uint8_t **start, uint8_t **end);
    uint16_t rewind();
    bool has_next();
};

typedef union {
    Heartbeat heartbeat;
    OpenCluster open;
    CloseCluster close;
    Follow follow;
    Accept accept;
    SendAcks acks;
    Datagram datagram;
} Body;

/**
 * Message struct that gets sent over the wire, potentially with data following
 * it (if it is a datagram message).
 */
#pragma pack(push, 1)
struct Message {
    Header hdr;
    Body body;
    // ... there might be data here depending on the message type.

    /**
     * This returns the size in bytes of the message portion, but does not
     * account for any variable-length data sent afer (e.g., In datagram).
     */
    size_t size();
};
#pragma pack(pop)

}  // namespace multihop
}  // namespace net
