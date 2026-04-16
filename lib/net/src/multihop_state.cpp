#include "multihop_state.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "multihop.h"

namespace net {

namespace multihop {

MultihopState::MultihopState()
    : address(UNKNOWN_ADDR),
      ring(UNKNOWN_RING),
      role(Role::NotInit),
      slot_assignments(nullptr),
      slot_count(0),
      acks(nullptr),
      // clusterheads "assign" themselves to slot 0 of their virtual TDMA window
      slots_assigned(1),
      neighbors{0, 0},
      clusterhead_p(0),
      ch_credit(0),
      clusterhead_ring(UNKNOWN_RING),
      clusterhead_addr(UNKNOWN_ADDR),
      upstream_start_slot(0),
      upstream_relative_slot(0),
      upstream_ack_slot(0),
      downstream_start_slot(0),
      rounds_without_lower(0) {}

void MultihopState::join_cluster(Ring cluster_ring, Address cluster_addr) {
    this->role = Role::Follower;
    this->clusterhead_ring = cluster_ring;
    this->clusterhead_addr = cluster_addr;
    if (clusterhead_ring < this->ring) {
        this->ring = clusterhead_ring + 1;
    }
}

void MultihopState::reset_neighbors() {
    memset(&this->neighbors, 0, sizeof(this->neighbors));
}

void MultihopState::add_neighbor(Ring neighbor) {
    // Unknown-ring neighbors provide no useful routing information
    if (UNKNOWN_RING == neighbor) {
        return;
    }
    // index 0 count will always be non-zero unless it just got reset
    if (!this->neighbors.counts[0]) {
        this->neighbors.ring_start = neighbor;
    }

    size_t counts_sz = sizeof(this->neighbors.counts);
    size_t end_ring = this->neighbors.ring_start + counts_sz;
    if (this->neighbors.counts[0] && neighbor >= end_ring) {
        // ignore neighbors outside of the range
        return;
    } else if (neighbor < this->neighbors.ring_start) {
        // shift existing data right to make room for the new lower ring
        size_t offset = this->neighbors.ring_start - neighbor;
        if (offset >= counts_sz) {
            // Offset too large to shift; reset and start fresh
            memset(this->neighbors.counts, 0, counts_sz);
            this->neighbors.ring_start = neighbor;
        } else {
            // "Shift" lower slots into higher ones, with the new neighbor
            // entering into the lowest spot.
            for (size_t i = counts_sz - 1; i >= offset; --i) {
                this->neighbors.counts[i] = this->neighbors.counts[i - offset];
            }
            for (size_t i = 0; i < offset; ++i) {
                this->neighbors.counts[i] = 0;
            }
            this->neighbors.ring_start = neighbor;
        }
    }

    size_t index = neighbor - this->neighbors.ring_start;
    ++this->neighbors.counts[index];
}

void MultihopState::with_address(Address address) {
    this->address = address;
    if (GATEWAY_ADDR == address) {
        this->role = Role::Gateway;
        this->ring = GATEWAY_RING;
    } else {
        this->role = Role::Follower;
    }
}

void MultihopState::with_acks_array(uint32_t* acks, size_t sz) {
    this->acks = acks;
    this->slot_count = sz;
}

void MultihopState::clusterhead_probability(uint8_t p) {
    this->clusterhead_p = p;
}

void MultihopState::reset_ring(Ring ring) { this->ring = ring; }

void MultihopState::update_ring_from_neighbors() {
    if (GATEWAY_ADDR == this->address) {
        this->ring = GATEWAY_RING;
        return;
    }
    // Find the lowest ring among neighbors
    Ring lowest = UNKNOWN_RING;
    for (size_t i = 0; i < sizeof(this->neighbors.counts); ++i) {
        if (this->neighbors.counts[i] > 0) {
            lowest = this->neighbors.ring_start + i;
            break;
        }
    }
    if (lowest < this->ring) {
        // Found a lower-ring neighbor: set ring = lowest + 1, reset hysteresis
        this->ring = lowest + 1;
        this->rounds_without_lower = 0;
    } else if (lowest >= this->ring && lowest != UNKNOWN_RING) {
        // All neighbors at same or higher ring
        ++this->rounds_without_lower;
        if (this->rounds_without_lower >= RING_HYSTERESIS_THRESHOLD) {
            // After N consecutive rounds, allow ring to drift up
            this->ring = lowest + 1;
            this->rounds_without_lower = 0;
        }
        // Otherwise keep current ring (hysteresis)
    } else {
        // No neighbors at all
        ++this->rounds_without_lower;
        if (this->rounds_without_lower >= RING_HYSTERESIS_THRESHOLD) {
            this->ring = UNKNOWN_RING;
            this->rounds_without_lower = 0;
        }
    }
}

bool MultihopState::initialized() { return this->role != Role::NotInit; }

bool MultihopState::acts_as_clusterhead() {
    return this->role == Role::Gateway || this->role == Role::Clusterhead;
}

bool MultihopState::acts_as_follower() {
    return this->role == Role::Follower || this->role == Role::Clusterhead;
}
}  // namespace multihop
}  // namespace net
