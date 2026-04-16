/**
 * multihop.h
 *
 * Multihop network layer protocol built off TDMA which funnels all messages to
 * the lowest ID nodes in the network.
 */
#pragma once
#include <stdint.h>

#include "multihop_defs.h"
#include "multihop_messages.h"
#include "multihop_state.h"
#include "net.h"

namespace net {
namespace multihop {

using ack_t = uint32_t;

// Struct to keep track of top & second candidates
struct ClusterheadCandidates {
    Address addresses[2];
    Ring rings[2];
    uint32_t scores[2];

    void reset();
    void update(Address address, Ring ring, uint32_t score);
    void top(Address *address, Ring *ring);
    void second(Address *address, Ring *ring);
};

enum struct RC : uint8_t {
    Ok,
    NotInit,
    NoData,
    LinkFull,
    LinkError,
    Closed,
    Empty,
    NotInWindow,
    NotMyTurn,
    InvalidSlot,
    NoClusterhead,
    // All the slots of this clusterhead are occupied
    SlotsFull,
    // Acks array provided to `init` is not big enough for all slots
    AcksArrayLen,
};

// ---------------------------------------------------
// State-management functions
// ---------------------------------------------------

/**
 * Initialize all parameters in this layer and those below it.
 *
 * @param node, mac, link: Passed through to link layer.
 * @param clusterhead_probability: Probability this node will volunteer as a
 * clusterhead during advertising window.
 */
RC init(NodeParams *node, MacParams *mac, LinkParams *link, ack_t *acks,
        Address *slots, size_t slot_count,
        uint8_t clusterhead_probability = 64);

/**
 * Change clusterhead probability. You might do this to conserve power, or if
 * the neighbor density changes.
 *
 * @param p: Probability (`p` / 255) of volunteering to be a clusterhead.
 */
void set_clusterhead_probability(uint8_t p);

/**
 * @returns (MultihopState*) pointer to multihop state.
 */
MultihopState *get_state();

// ---------------------------------------------------
// Functions for queuing messages
// ---------------------------------------------------

/**
 * Announce this node as a clusterhead in the next round.
 */
RC open_cluster();

/**
 * Request to join the clusterhead at the address provided.
 */
RC follow(Address clusterhead);

/**
 * Accept `follower` node's request to join the cluster.
 */
RC accept(Address follower);

/**
 * Announce this node will no longer be operating as a clusterhead, or
 * renominate self again.
 */
RC close_cluster(bool renominate);

/**
 * Send ACKs for every TDMA slot in the last window.
 *
 * @param ack_start: Argument for starting index within the `acks` array.
 * @param ack_end: Out-parameter with the ending index from this transmission if
 * successful.
 */
RC send_acks(uint8_t ack_start, uint8_t *ack_end);

/**
 * Send heartbeat during first phase of advertisement.
 */
RC send_heartbeat();

/**
 * Send a datagram to the `dst` address opened.
 *
 * @param iter: Pointer to an iterator object which helps perform fragmentation
 * of large packets.
 */
RC send_datagram(DatagramIterator *iter);

// Time-keeping functions

RC epoch_and_offset(uint8_t n, uint32_t *time, uint32_t *offset);
RC advertisement_slot(uint32_t *time, uint32_t *offset);
RC clusterhead_slot(uint32_t *time, uint32_t *offset);
RC my_slot(uint32_t *time, uint32_t *offset);

// ---------------------------------------------------
// Functions actually sending/receiving messages.
// ---------------------------------------------------

/**
 * >> ONLY VALID IN ADVERTISING PHASE (global slot 0)
 *
 * Perform advertising slot actions such as:
 *  - Announcing clusterhead status.
 *  - Choosing a clusterhead.
 *  - Eavesdrop on other node's communications to build ring list.
 */
RC do_advertise();

/**
 * >> ONLY VALID IN SLOT `start_slot + relative_slot` (slot 0 of virtual TDMA
 * window)
 *
 * Send queued messages to clusterhead.
 */
RC do_follower_send();

/**
 * >> ONLY VALID IN LAST SLOT OF VIRTUAL TDMA WINDOW
 *    (slot `start_slot + superframe_slot_count/2 - 1`)
 *
 * Receive announcements from the clusterhead aimed at its followers.
 * Processes ACKs and CloseCluster messages.
 */
RC do_follower_recv();

/**
 * >> ONLY VALID IN LAST SLOT OF VIRTUAL TDMA WINDOW
 *    (slot `downstream_start_slot + vtdma_window - 1`)
 *
 * For gateway/pure-CH: send acks + CloseCluster in the last slot of the
 * downstream window.
 */
RC do_clusterhead_send();

/**
 * >> VALID IN VIRTUAL TDMA SLOTS [start_slot+1, start_slot+window-2]
 *
 * Receive datagrams from followers, update ACKs, and re-queue data for
 * forwarding to this clusterhead's own clusterhead.
 */
RC do_clusterhead_recv();

/**
 * Send any queued messages (doesn't check whether this is actually the node's
 * turn or not).
 */
RC flush();

}  // namespace multihop
}  // namespace net
