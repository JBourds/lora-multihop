#pragma once
#include "multihop_messages.h"
#include "net.h"

namespace net {
namespace multihop {

enum struct Role : uint8_t {
    Gateway,
    Clusterhead,
    Follower,
    NotInit,
};

struct MultihopState {
    Address address;
    Ring ring;
    Role role;

    // `Clusterhead` state information
    // Buffer where acks can be stored. Must be >= length to the allowed number
    // of slots.
    Address* slot_assignments;
    size_t slot_count;
    uint32_t* acks;
    // Count of the number of slots assigned so far in the upcoming window.
    uint8_t slots_assigned;
    // Counts of the number of discovered neighbors.
    RingList neighbors;
    // Threshold value used to determine whether this node becomes a leader
    uint8_t clusterhead_p;
    // Credit counter for fair CH rotation: incremented each SF the node is NOT
    // CH, reset to 0 when elected.  Scales election probability upward so that
    // long-waiting nodes are increasingly likely to win.
    uint8_t ch_credit;

    // Upstream (follower) state
    Ring clusterhead_ring;
    Address clusterhead_addr;
    uint8_t upstream_start_slot;
    uint8_t upstream_relative_slot;
    // Absolute slot where the upstream CH sends acks (we listen here)
    uint8_t upstream_ack_slot;

    // Downstream (clusterhead) state
    uint8_t downstream_start_slot;

    // Ring hysteresis: count consecutive rounds without a lower-ring neighbor
    // before allowing ring to increase. Prevents oscillation in lossy networks.
    uint8_t rounds_without_lower;
    static const uint8_t RING_HYSTERESIS_THRESHOLD = 3;

    MultihopState();

    /**
     * Join the cluster with clusterhead advertising ring `cluster_ring` and
     * node address `cluster_addr`.
     *
     * @param cluster_ring: Ring number advertised by clusterhead.
     * @param cluster_addr: Clusterhead's unique node ID.
     */
    void join_cluster(Ring cluster_ring, Address cluster_addr);

    /**
     * @param p: Set probability with (`p` / 255) chance of volunteering as a
     * clusterhead during election.
     */
    void clusterhead_probability(uint8_t p);

    /**
     * Initialize node's address.
     */
    void with_address(Address address);

    /**
     * Initialize the acks array.
     */
    void with_acks_array(uint32_t* acks, size_t sz);

    /**
     * Reset ring to specified level (e.g., If our lowest ring neighbor dies).
     */
    void reset_ring(Ring ring = UNKNOWN_RING);

    /**
     * Reset neighboring rings count.
     */
    void reset_neighbors();

    /**
     * Handle adding/updating rings list.
     */
    void add_neighbor(Ring neighbor);

    /**
     * Update ring based on neighbor discovery results with hysteresis.
     * Only increases ring after RING_HYSTERESIS_THRESHOLD consecutive rounds
     * without a lower-ring neighbor.
     */
    void update_ring_from_neighbors();

    bool initialized();

    /**
     * True if this node manages a downstream VTDMA window
     * (Gateway or Clusterhead).
     */
    bool acts_as_clusterhead();

    /**
     * True if this node participates as a follower in an upstream window
     * (Follower or Clusterhead). Gateway is the only CH that does not
     * also act as a follower.
     */
    bool acts_as_follower();
};
}  // namespace multihop
}  // namespace net
