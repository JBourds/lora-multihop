#pragma once
#include "net.h"

namespace net {
namespace network {

enum struct RC : uint8_t {
    Ok,
    Closed,
};

enum struct MsgType : uint8_t {
    RequestRoutes,
    RoutesResponse,
    Close,
    Data,
};

struct DatagramHeader {
    // Protocol being used.
    Protocol protocol = Protocol::Multihop;
    // Type of message.
    MsgType type;
    // Number of parts in this message.
    uint8_t count;
    // Number `n` / `count` transmissions.
    uint8_t sequence;
    // Size of the datagram chunk within this frame in bytes.
    uint16_t sz;
};

/**
 * Message containing the desired route `src` -> `dst`.
 * Initates responses from nodes which have the `dst` node in `nhops` or fewer.
 */
struct RequestRoutes {
    // address of the node which just opened the connection.
    Address src;
    // address of the node trying to be reached.
    Address dst;
    // accept routes <= `nhops` hops away.
    uint8_t nhops;
};

/**
 * Message containing a bit array with 1s/0s for every node the `src` knows will
 * hear it knows of.
 */
struct RoutesResponse {
    // source node
    Address src;
    // number of hops away this node believes each of its connections are.
    uint8_t nhops;
    // bit array with a 1 for each ID if there is a connection
    uint8_t connections[256 / 8];
};

/**
 * Signal this node is closed and can no longer be relied on to route or receive
 * messages until some point in the future.
 */
struct CloseMsg {
    // address of the node closing down.
    Address address;
    // future time this node will come back online.
    uint32_t wakeup;
};

/**
 * Connection-oriented over broadcast link layer.
 *
 * @param address: Destination address for communications.
 */
RC open(Address address);

/**
 * Break up `data` into smaller chunks if needed and queue them for transmission
 * once it is this node's turn to speak.
 *
 * @param data: Raw datagram bytes.
 * @param sz: Number of bytes to be sent.
 */
RC send(uint8_t *data, size_t sz);

/**
 * Try to receive addressed messages into `buf`.
 * Each call to `recv` will recieve one part of the `count` components the
 * message has been split into.
 *
 * @param buf: Receive buffer.
 * @param sz: Receive buffer size in bytes.
 */
RC recv(uint8_t *buf, size_t sz);

/**
 * Attempt to parse the next part of the datagram within `recv_buf`.
 *
 * @param recv_buf: Pointer to the first frame header within a series of
 * chunks received into `recv_buf` by calling the `recv` method.
 * @param recv_sz: Total number of bytes in the receive buffer.
 * @param offset: Current offset into the buffer. Will be incremented on
 * successful reads. On an error, this gets set to just beyond the end of the
 * receive buffer (`recv_buf` + `recv_sz`).
 *
 * @returns (DatagramHeader*): Pointer to the header for the next chunk. The
 * data itself can be accessed by recasting the pointer to (uint8_t*) and adding
 * `sizeof(DatagramHeader)`. If another chunk cannot be parsed, returns
 * `nullptr`.
 */
DatagramHeader *next_datagram_chunk(uint8_t *recv_buf, size_t recv_sz,
                                    size_t *offset);

/**
 * Send a routes request message.
 */
RC request_routes();

/**
 * Send a message with this node's routes.
 */
RC send_routes();

/**
 * Act as a router to any incoming messages.
 *
 * @param duration_ms: Duration to route messages for.
 */
RC route(uint32_t duration_ms);

/**
 * Make sure queued messages are sent by blocking until our turn in the TDMA
 * interval.
 */
RC flush();

/**
 * Close existing connection.
 */
RC close();

}  // namespace network
}  // namespace net
