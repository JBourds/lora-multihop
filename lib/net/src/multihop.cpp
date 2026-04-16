#include "multihop.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "link_layer.h"
#include "multihop_defs.h"
#include "net.h"
#include "time_helpers.h"

#ifdef SIMULATE
#include <stdio.h>

#include "arduino_stubs.h"
#define DBG(fmt, ...) \
    fprintf(stderr, "[DBG Node %u] " fmt "\n", STATE.address, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#define min(a, b) (a > b ? b : a)

namespace net {

extern const Address UNKNOWN_ADDR;

namespace multihop {

void run_neighbor_discovery(uint32_t duration_ms);
bool run_clusterhead_announcement(uint32_t duration_ms, uint8_t eligible_ring,
                                  ClusterheadCandidates *candidates,
                                  bool ch_earlier_round);
void run_clusterhead_joining(uint32_t duration_ms, bool is_clusterhead,
                             uint8_t eligible_ring,
                             ClusterheadCandidates *candidates);

const uint8_t ADVERTISEMENT_SLOT = 0;
const uint8_t ADV_SLOTS = 1;
const uint8_t NUM_ADV_ROUNDS = 3;
const Ring GATEWAY_RING = 0;
const Ring UNKNOWN_RING = 255;
// Maximum allowed time difference (seconds) when syncing to a clusterhead.
// Rejects large clock jumps that would indicate a corrupted timestamp.
const uint32_t MAX_TIME_SYNC_DRIFT_S = 60;

static MultihopState STATE;
static MacParams *MAC;

static uint8_t vtdma_window_size() {
    return (MAC->superframe_slot_count - ADV_SLOTS) / MAC->reuse_distance;
}

static Header make_header(MessageType type) {
    return {
        .sequence = time::now(),
        .src = STATE.address,
        .type = type,
    };
}

static Message make_msg(MessageType type, Body body) {
    return Message{
        .hdr = make_header(type),
        .body = body,
    };
}

/**
 * Send the message and, optionally, some bytes following it.
 */
static RC send(Message *msg, uint8_t *buf = nullptr, size_t sz = 0) {
    link::RC rc;
    if (nullptr == buf || 0 == sz) {
        rc = link::sendmsg(reinterpret_cast<uint8_t *>(msg), msg->size());
    } else {
        link::MessageVec p2(buf, sz);
        link::MessageVec p1(reinterpret_cast<uint8_t *>(msg), msg->size(), &p2);
        rc = link::sendmsg(&p1);
    }
    if (rc == link::RC::Ok) {
        return RC::Ok;
    } else if (rc == link::RC::BufSize) {
        return RC::LinkFull;
    } else {
        return RC::LinkError;
    }
}

static const uint8_t CREDIT_SCALE = 32;

static bool announce_clusterhead() {
    if (0 == STATE.clusterhead_p) {
        return false;
    }
    uint16_t effective_p = STATE.clusterhead_p;
    effective_p += static_cast<uint16_t>(STATE.ch_credit) * CREDIT_SCALE;
    if (UINT8_MAX < effective_p) {
        effective_p = UINT8_MAX;
    }
    uint8_t v = static_cast<uint8_t>(rand());
    return static_cast<uint8_t>(effective_p) >= v;
}

static uint32_t rand_delay_ms(uint32_t low, uint32_t high) {
    uint32_t v = static_cast<uint32_t>(rand());
    return v % (high - low + 1) + low;
}

static bool decode_msg(Message **msg, uint8_t *buf, size_t sz) {
    *msg = nullptr;
    size_t nbytes = sizeof(Header);
    if (nbytes > sz) {
        return false;
    }
    Header *hdr = reinterpret_cast<Header *>(buf);
    switch (hdr->type) {
        case MessageType::Heartbeat: {
            nbytes += sizeof(Heartbeat);
            break;
        }
        case MessageType::OpenCluster: {
            nbytes += sizeof(OpenCluster);
            break;
        }
        case MessageType::Follow: {
            nbytes += sizeof(Follow);
            break;
        }
        case MessageType::Accept: {
            nbytes += sizeof(Accept);
            break;
        }
        case MessageType::CloseCluster: {
            nbytes += sizeof(CloseCluster);
            break;
        }
        // These cases have variable-length data which shouldn't be copied in
        case MessageType::SendAcks: {
            nbytes += sizeof(SendAcks);
            if (nbytes > sz) {
                return false;
            }
            SendAcks *body = reinterpret_cast<SendAcks *>(buf + sizeof(Header));
            // half-open range, so <= inequality is correct
            if (body->end <= body->start) {
                return false;
            }
            uint8_t num_acks = body->end - body->start;
            size_t acks_size = num_acks * sizeof(decltype(STATE.acks[0]));
            if ((nbytes + acks_size) == sz) {
                *msg = reinterpret_cast<Message *>(buf);
                return true;
            }
            return false;
        }
        case MessageType::Datagram: {
            nbytes += sizeof(Datagram);
            if (nbytes > sz) {
                return false;
            }
            Datagram *body = reinterpret_cast<Datagram *>(buf + sizeof(Header));
            if ((nbytes + body->packet_bytes) == sz) {
                *msg = reinterpret_cast<Message *>(buf);
                return true;
            }
            return false;
        }
        default: {
            return false;
        }
    }
    if (nbytes == sz) {
        *msg = reinterpret_cast<Message *>(buf);
        return true;
    } else {
        return false;
    }
}

static void prepare_clusterhead() {
    const uint8_t end = STATE.slot_count - 1;
    // reset state
    memset(STATE.acks, 0, STATE.slot_count * sizeof(ack_t));
    memset(STATE.slot_assignments, 0, STATE.slot_count * sizeof(Address));
    // Assign ourselves to the last VTDMA slot
    STATE.slot_assignments[end] = STATE.address;
    STATE.slots_assigned = 1;

    // Inverted layout: deeper rings get earlier slots, gateway goes last.
    // This lets data convergecast to the gateway within a single superframe.
    uint8_t vtdma_window = vtdma_window_size();
    uint8_t inverted_index =
        MAC->reuse_distance - 1 - (STATE.ring % MAC->reuse_distance);
    STATE.downstream_start_slot = ADV_SLOTS + inverted_index * vtdma_window;
}

void ClusterheadCandidates::reset() {
    for (size_t i = 0; i < 2; ++i) {
        this->addresses[i] = UNKNOWN_ADDR;
        this->rings[i] = UNKNOWN_RING;
        this->scores[i] = 0;
    }
}

void ClusterheadCandidates::update(Address address, Ring ring, uint32_t score) {
    if (score > this->scores[0]) {
        this->addresses[1] = this->addresses[0];
        this->rings[1] = this->rings[0];
        this->scores[1] = this->scores[0];
        this->addresses[0] = address;
        this->rings[0] = ring;
        this->scores[0] = score;
    } else if (score > this->scores[1]) {
        this->addresses[1] = address;
        this->rings[1] = ring;
        this->scores[1] = score;
    }
}

void ClusterheadCandidates::top(Address *address, Ring *ring) {
    *address = this->addresses[0];
    *ring = this->rings[0];
}

void ClusterheadCandidates::second(Address *address, Ring *ring) {
    *address = this->addresses[1];
    *ring = this->rings[1];
}

RC init(NodeParams *node, MacParams *mac, LinkParams *l, ack_t *acks,
        Address *slots, size_t slot_count, uint8_t clusterhead_probability) {
    if (mac->superframe_slot_count > slot_count) {
        return RC::AcksArrayLen;
    }
    link::RC rc = link::init(node, mac, l);
    if (link::RC::Ok != rc) {
        return RC::LinkError;
    }
    MAC = mac;
    STATE.with_address(node->address);
    STATE.slot_assignments = slots;
    STATE.clusterhead_probability(clusterhead_probability);
    STATE.with_acks_array(acks, slot_count);
    // Gateway is always a clusterhead; initialize downstream state immediately
    // so that prepare_clusterhead() doesn't need to be called later (since
    // already_ch is always true for gateways in run_clusterhead_announcement).
    if (node->address == GATEWAY_ADDR) {
        prepare_clusterhead();
    }
    return RC::Ok;
}

void set_clusterhead_probability(uint8_t p) {
    STATE.clusterhead_probability(p);
}

MultihopState *get_state() {
    if (!STATE.initialized()) {
        return nullptr;
    }
    return &STATE;
}

RC epoch_and_offset(uint8_t n, uint32_t *time, uint32_t *offset) {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    link::RC rc = link::epoch_and_offset(n, time, offset);
    if (rc == link::RC::Ok) {
        return RC::Ok;
    } else if (rc == link::RC::NotInit) {
        return RC::NotInit;
    } else if (rc == link::RC::InvalidSlot) {
        return RC::InvalidSlot;
    } else if (rc == link::RC::NotInWindow) {
        return RC::NotInWindow;
    } else {
        return RC::LinkError;
    }
}

RC advertisement_slot(uint32_t *time, uint32_t *offset) {
    return epoch_and_offset(ADVERTISEMENT_SLOT, time, offset);
}

RC clusterhead_slot(uint32_t *time, uint32_t *offset) {
    if (UNKNOWN_ADDR == STATE.clusterhead_addr) {
        return RC::NoClusterhead;
    }
    return epoch_and_offset(STATE.downstream_start_slot, time, offset);
}

RC my_slot(uint32_t *time, uint32_t *offset) {
    if (UNKNOWN_ADDR == STATE.clusterhead_addr) {
        return RC::NoClusterhead;
    }
    uint8_t slot = STATE.upstream_start_slot + STATE.upstream_relative_slot;
    return epoch_and_offset(slot, time, offset);
}

RC flush() {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    link::RC rc = link::flush();
    if (rc == link::RC::Ok) {
        return RC::Ok;
    } else if (rc == link::RC::NotInWindow) {
        return RC::NotInWindow;
    } else {
        return RC::LinkError;
    }
}

// message-sending methods

RC open_cluster() {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    Body body;
    body.open = {
        .ring = STATE.ring,
        .node = STATE.address,
        .neighbors = STATE.neighbors,
    };
    Message msg = make_msg(MessageType::OpenCluster, body);
    return send(&msg);
}

RC follow(Address clusterhead) {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    Body body;
    body.follow = {
        .clusterhead = clusterhead,
        .follower = STATE.address,
    };
    Message msg = make_msg(MessageType::Follow, body);
    return send(&msg);
}

RC accept(Address follower) {
    Body body;
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    uint8_t vtdma_window = vtdma_window_size();
    // Duplicate guard: if this follower already has a slot, re-send the same
    // Accept rather than allocating a new slot.
    for (uint8_t i = 0; i < STATE.slots_assigned; ++i) {
        if (STATE.slot_assignments[i] == follower) {
            body.accept = {
                .accepted = true,
                .follower = follower,
                .start_slot = STATE.downstream_start_slot,
                .relative_slot = i,
                .remote_time_s = time::now(),
            };
            Message msg = make_msg(MessageType::Accept, body);
            return send(&msg);
        }
    }
    if (STATE.slots_assigned >= vtdma_window) {
        body.accept = {
            .accepted = false,
            .follower = follower,
            .start_slot = 0,
            .relative_slot = 0,
            .remote_time_s = time::now(),
        };
        Message msg = make_msg(MessageType::Accept, body);
        send(&msg);
        return RC::SlotsFull;
    }
    STATE.slot_assignments[STATE.slots_assigned] = follower;
    uint8_t relative_slot = STATE.slots_assigned++;
    body.accept = {
        .accepted = true,
        .follower = follower,
        .start_slot = STATE.downstream_start_slot,
        .relative_slot = relative_slot,
        .remote_time_s = time::now(),
    };
    Message msg = make_msg(MessageType::Accept, body);
    return send(&msg);
}

RC close_cluster(bool renominate) {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    Body body;
    // TODO: Nominate best candidate if we aren't voluntering again
    body.close = {
        .nomination = renominate ? STATE.address : UNKNOWN_ADDR,
        .neighbors = STATE.neighbors,
    };
    Message msg = make_msg(MessageType::CloseCluster, body);
    return send(&msg);
}

RC send_heartbeat() {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    Body body;
    body.heartbeat = {.ring = STATE.ring};
    Message msg = make_msg(MessageType::Heartbeat, body);
    return send(&msg);
}

RC send_acks(uint8_t ack_start, uint8_t *ack_end) {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    Body body;
    // figure out how many acks can fit in this message
    size_t avail = link::tx_bytes_available();
    size_t msg_sz = sizeof(Header) + sizeof(SendAcks);
    if (avail <= msg_sz) {
        *ack_end = ack_start;
        return RC::LinkFull;
    }
    size_t num_acks = (avail - msg_sz) / sizeof(ack_t);
    num_acks = min(num_acks, static_cast<size_t>(UINT8_MAX - ack_start));
    // Never read past the actual ack buffer
    if (ack_start + num_acks > STATE.slot_count) {
        num_acks =
            (ack_start < STATE.slot_count) ? STATE.slot_count - ack_start : 0;
    }
    // exclusive end range
    *ack_end = ack_start + num_acks;
    body.acks = {
        .start = ack_start,
        .end = *ack_end,
    };
    Message msg = make_msg(MessageType::SendAcks, body);
    return send(&msg, reinterpret_cast<uint8_t *>(STATE.acks + ack_start),
                num_acks * sizeof(ack_t));
}

RC send_datagram(DatagramIterator *iter) {
    if (!STATE.initialized()) {
        return RC::NotInit;
    }
    Body body;
    uint8_t *start, *end;
    if (iter->next(&body.datagram, &start, &end)) {
        if (iter->last_msg_sz > link::tx_bytes_available()) {
            iter->rewind();
            return RC::LinkFull;
        }
        Message msg = make_msg(MessageType::Datagram, body);
        return send(&msg, start, iter->last_msg_sz);
    } else {
        return RC::NoData;
    }
}

void run_neighbor_discovery(uint32_t duration_ms) {
    uint8_t *buf;
    size_t sz;
    STATE.reset_neighbors();
    time::MsDeadline phase_deadline(duration_ms);

    // Nodes with known ring info (gateway, or any node after the first SF)
    // can announce immediately with jitter.  Nodes at UNKNOWN_RING defer
    // until they hear a neighbor so they don't waste a transmission
    // advertising ring=UNKNOWN.
    bool announced = false;
    bool has_ring_info = (UNKNOWN_RING != STATE.ring);
    time::MsDeadline announce_deadline(
        GATEWAY_RING == STATE.ring ? 0 : rand_delay_ms(0, duration_ms / 4));

    DBG("neighbor discovery: duration=%lu ms, ring=%u",
        (unsigned long)duration_ms, STATE.ring);

    while (!phase_deadline.reached()) {
        bool time_to_announce = !announced && announce_deadline.reached();
        bool try_to_announce = has_ring_info && time_to_announce;
        uint32_t timeout_ms = !announced ? announce_deadline.millis_until()
                                         : phase_deadline.millis_until();

        // Process incoming heartbeats
        link::RC rc = link::recv(timeout_ms);
        while (link::RC::Ok == rc &&
               link::RC::Ok == (rc = link::getmsg(&buf, &sz))) {
            Message *msg;
            bool valid = decode_msg(&msg, buf, sz) && msg != nullptr;
            if (valid && msg->hdr.type == MessageType::Heartbeat) {
                DBG("  heard heartbeat from addr=%u ring=%u", msg->hdr.src,
                    msg->body.heartbeat.ring);
                STATE.add_neighbor(msg->body.heartbeat.ring);
                // update flags for whether we should try to announce
                has_ring_info = true;
                time_to_announce = !announced && announce_deadline.reached();
                try_to_announce = has_ring_info && time_to_announce;
            } else if (valid) {
                DBG("  got non-heartbeat msg type=%d from addr=%u during "
                    "discovery",
                    static_cast<int>(msg->hdr.type), msg->hdr.src);
            }
        }

        STATE.update_ring_from_neighbors();
        if (try_to_announce) {
            RC src = send_heartbeat();
            if (RC::Ok == src) {
                RC frc = flush();
                DBG("  heartbeat sent (flush rc=%d)", static_cast<int>(frc));
            } else {
                DBG("  heartbeat send failed rc=%d", static_cast<int>(src));
            }
            announced = true;
        }
    }
}

bool run_clusterhead_announcement(uint32_t duration_ms, uint8_t eligible_ring,
                                  ClusterheadCandidates *candidates,
                                  bool ch_earlier_round) {
    // Only announce if our ring matches the eligible ring for this round.
    // Gateways are always clusterheads (hardware-determined role, not elected).
    bool is_clusterhead = false;
    if (STATE.ring == eligible_ring) {
        is_clusterhead =
            (STATE.role == Role::Gateway) || announce_clusterhead();
    }
    // Only call prepare_clusterhead() on first promotion (this round).
    // Nodes that became CH in an earlier round already have their downstream
    // state set up; they continue accepting followers in the joining phase
    // but do NOT re-announce (a repeat OpenCluster adds no information since
    // radio range hasn't changed).
    if (is_clusterhead && !ch_earlier_round) {
        if (STATE.role != Role::Gateway) {
            STATE.role = Role::Clusterhead;
        }
        prepare_clusterhead();
    }
    DBG("CH announcement (round ring=%u): is_ch=%d my_ring=%u", eligible_ring,
        is_clusterhead, STATE.ring);
    bool need_to_announce = is_clusterhead && !ch_earlier_round;
    time::MsDeadline phase_deadline = time::MsDeadline(duration_ms);
    time::MsDeadline announce_deadline =
        time::MsDeadline(rand_delay_ms(0, duration_ms));

    // keep track of top 2
    candidates->reset();
    while (!phase_deadline.reached()) {
        uint32_t timeout_ms = (need_to_announce && !announce_deadline.reached())
                                  ? announce_deadline.millis_until()
                                  : phase_deadline.millis_until();
        link::RC rc = link::recv(timeout_ms);
        DBG("  announcement recv rc=%d timeout=%lu", static_cast<int>(rc),
            (unsigned long)timeout_ms);
        // announce this node as a clusterhead
        if (need_to_announce && announce_deadline.reached()) {
            RC arc = open_cluster();
            if (RC::Ok == arc) {
                arc = flush();
            }
            DBG("  announcement flush rc=%d", static_cast<int>(arc));
            need_to_announce = (RC::Ok != arc);
        }
        // process any received messages
        uint8_t *buf;
        size_t sz;
        while (link::RC::Ok == (rc = link::getmsg(&buf, &sz))) {
            Message *msg;
            bool valid = decode_msg(&msg, buf, sz) && msg != nullptr;
            if (valid && msg->hdr.type == MessageType::OpenCluster &&
                msg->hdr.src != STATE.address) {
                Ring node_ring = msg->body.open.ring;
                uint32_t score = msg->body.open.neighbors.score(node_ring);
                DBG("  heard OpenCluster from addr=%u ring=%u score=%lu",
                    msg->hdr.src, node_ring, (unsigned long)score);
                candidates->update(msg->hdr.src, node_ring, score);
                // Don't volunteer if a peer on the same ring already did
                if (is_clusterhead && node_ring == STATE.ring) {
                    // Use address as a deterministic tiebreaker
                    if (need_to_announce || STATE.address < msg->hdr.src) {
                        is_clusterhead = false;
                        need_to_announce = false;
                    }
                }
            } else if (valid) {
                DBG("  announcement got msg type=%d from addr=%u",
                    static_cast<int>(msg->hdr.type), msg->hdr.src);
            }
        }
    }
    return is_clusterhead;
}

void run_clusterhead_joining(uint32_t duration_ms, bool is_clusterhead,
                             uint8_t eligible_ring,
                             ClusterheadCandidates *candidates) {
    // Any non-gateway node can try to follow an announcing CH. This includes
    // Clusterhead-role nodes (relays) which need an upstream CH for forwarding.
    // The round structure provides natural ordering: round 0 CHs announce
    // first, so closer nodes join first; round 1 newly promoted CHs announce
    // next, allowing deeper nodes to join.
    bool eligible_follower = STATE.acts_as_follower();

    uint8_t *buf;
    size_t sz;
    Message *msg;
    Address target_node;
    Ring target_ring;
    candidates->top(&target_node, &target_ring);
    DBG("CH joining (eligible_ring=%u): is_ch=%d eligible_follower=%d "
        "target=%u target_ring=%u",
        eligible_ring, is_clusterhead, eligible_follower, target_node,
        target_ring);

    bool try_to_follow = eligible_follower;
    bool joined = false;
    bool tried_second = false;
    time::MsDeadline follow_deadline(rand_delay_ms(0, duration_ms / 2));
    time::MsDeadline phase_deadline(duration_ms);
    DBG("  joining: duration=%lu follow_until=%lu phase_until=%lu "
        "raw_millis=%llu",
        (unsigned long)duration_ms,
        (unsigned long)follow_deadline.millis_until(),
        (unsigned long)phase_deadline.millis_until(),
        (unsigned long long)millis());
    uint8_t join_loop_count = 0;
    while (!phase_deadline.reached()) {
        if (try_to_follow && follow_deadline.reached()) {
            if (target_node != UNKNOWN_ADDR) {
                DBG("  sending Follow to addr=%u", target_node);
                follow(target_node);
                flush();
            } else {
                DBG("  no candidate to follow");
            }
            try_to_follow = false;
        }
        uint32_t timeout_ms = follow_deadline.reached()
                                  ? phase_deadline.millis_until()
                                  : follow_deadline.millis_until();
        if (join_loop_count < 5) {
            DBG("  join iter %u: try=%d follow_reached=%d timeout=%lu "
                "phase_left=%lu raw_millis=%llu",
                join_loop_count, try_to_follow, follow_deadline.reached(),
                (unsigned long)timeout_ms,
                (unsigned long)phase_deadline.millis_until(),
                (unsigned long long)millis());
        }
        ++join_loop_count;
        link::RC rc = link::recv(timeout_ms);
        if (join_loop_count <= 5) {
            DBG("  join recv rc=%d raw_millis=%llu phase_left=%lu",
                static_cast<int>(rc), (unsigned long long)millis(),
                (unsigned long)phase_deadline.millis_until());
        }
        while (link::RC::Ok == (rc = link::getmsg(&buf, &sz))) {
            bool valid = decode_msg(&msg, buf, sz) && msg != nullptr;
            if (!valid) {
                break;
            }
            if (is_clusterhead && msg->hdr.type == MessageType::Follow &&
                msg->body.follow.clusterhead == STATE.address) {
                DBG("  got Follow from addr=%u, accepting", msg->hdr.src);
                accept(msg->hdr.src);
                flush();
            } else if (msg->hdr.type == MessageType::Follow) {
                DBG("  got Follow from %u for ch=%u (I'm %s ch)", msg->hdr.src,
                    msg->body.follow.clusterhead, is_clusterhead ? "a" : "not");
            } else if (msg->hdr.type == MessageType::Accept) {
                DBG("  got Accept from %u for follower=%u accepted=%d "
                    "(target=%u me=%u)",
                    msg->hdr.src, msg->body.accept.follower,
                    msg->body.accept.accepted, target_node, STATE.address);
            }
            if (!joined && eligible_follower &&
                msg->hdr.type == MessageType::Accept &&
                msg->hdr.src == target_node &&
                msg->body.accept.follower == STATE.address) {
                if (msg->body.accept.accepted) {
                    joined = true;
                    STATE.clusterhead_ring = target_ring;
                    STATE.clusterhead_addr = target_node;
                    STATE.upstream_start_slot = msg->body.accept.start_slot;
                    STATE.upstream_relative_slot =
                        msg->body.accept.relative_slot;
                    // Ack slot is always the last slot in the CH's window.
                    STATE.upstream_ack_slot =
                        msg->body.accept.start_slot + vtdma_window_size() - 1;
                    DBG("  JOINED cluster of addr=%u start_slot=%u "
                        "rel_slot=%u ack_slot=%u",
                        target_node, STATE.upstream_start_slot,
                        STATE.upstream_relative_slot, STATE.upstream_ack_slot);
                    // Time sync with sanity check
                    uint32_t local_time = time::now();
                    uint32_t remote_time = msg->body.accept.remote_time_s;
                    uint32_t diff = (remote_time > local_time)
                                        ? (remote_time - local_time)
                                        : (local_time - remote_time);
                    if (diff <= MAX_TIME_SYNC_DRIFT_S) {
                        time::set(remote_time);
                    }
                    // If this node won the CH election, promote to CH.
                    // Guard with acts_as_clusterhead(): if we already have
                    // downstream followers from an earlier round, calling
                    // prepare_clusterhead() would wipe slot_assignments
                    // Use STATE rather than the local
                    // is_clusterhead param, which only reflects this round.
                    if (!STATE.acts_as_clusterhead() &&
                        announce_clusterhead()) {
                        STATE.role = Role::Clusterhead;
                        prepare_clusterhead();
                        DBG("  promoted to Clusterhead: "
                            "downstream_start_slot=%u",
                            STATE.downstream_start_slot);
                    }
                } else {
                    if (!tried_second) {
                        candidates->second(&target_node, &target_ring);
                        tried_second = true;
                        if (target_node != UNKNOWN_ADDR) {
                            try_to_follow = true;
                            follow_deadline = time::MsDeadline(
                                rand_delay_ms(0, duration_ms / 3));
                        }
                    }
                    break;
                }
            }
        }
    }
}

RC do_advertise() {
    int16_t slot = link::active_slot();
    if (slot != ADVERTISEMENT_SLOT) {
        return RC::InvalidSlot;
    }
    // Advertising is always done on the default channel
    link::set_cdma_code(link::DEFAULT_CODE);

    // Reset cluster state so every node re-joins fresh each superframe.
    // Without this, nodes retain stale clusterhead_addr/role from the
    // previous superframe and skip the joining phase entirely.
    if (STATE.role != Role::Gateway) {
        STATE.role = Role::Follower;
    }
    STATE.clusterhead_addr = UNKNOWN_ADDR;
    STATE.clusterhead_ring = UNKNOWN_RING;
    STATE.upstream_start_slot = 0;
    STATE.upstream_relative_slot = 0;
    STATE.upstream_ack_slot = 0;
    // Zero acks to prevent stale sequence numbers from prior superframes
    memset(STATE.acks, 0, STATE.slot_count * sizeof(ack_t));

    // Use the full slot duration for consistent phase lengths regardless of
    // when within the slot we were called.
    uint32_t total_ms = MAC->slot_duration() * 1000;
    // Single discovery phase: nodes wait to hear a neighbor before
    // announcing, so ring info cascades outward naturally.
    uint32_t discovery_ms = total_ms * 2 / 5;
    uint32_t round_ms = (total_ms - discovery_ms) / (NUM_ADV_ROUNDS * 2);

    // 1. Neighbor discovery
    run_neighbor_discovery(discovery_ms);

    // 2. Iterative rounds of announcement + joining
    // Each round targets the next ring level outward.
    // Round 0: ring 0 (gateway) announces, ring 1 joins.
    // Round 1: ring 1 CHs announce, ring 2 joins. etc.
    bool is_clusterhead = false;
    for (uint8_t round = 0; round < NUM_ADV_ROUNDS; ++round) {
        uint8_t eligible_ring = round;
        ClusterheadCandidates candidates;
        bool ch_this_round = run_clusterhead_announcement(
            round_ms, eligible_ring, &candidates, is_clusterhead);
        is_clusterhead = is_clusterhead || ch_this_round;
        run_clusterhead_joining(round_ms, is_clusterhead, eligible_ring,
                                &candidates);
    }

    // Credit bookkeeping: gateways are always CH so credits don't apply.
    if (STATE.role != Role::Gateway) {
        if (STATE.acts_as_clusterhead()) {
            STATE.ch_credit = 0;
        } else if (255 > STATE.ch_credit) {
            ++STATE.ch_credit;
        }
    }

    return RC::Ok;
}

RC do_follower_send() {
    int16_t slot = link::active_slot();
    uint8_t my_slot = STATE.upstream_start_slot + STATE.upstream_relative_slot;
    if (slot != my_slot) {
        return RC::InvalidSlot;
    }
    // Set CDMA code based on our relative slot offset
    link::set_cdma_code(STATE.upstream_relative_slot);
    if (link::RC::Ok != link::flush()) {
        return RC::LinkError;
    }
    return RC::Ok;
}

RC do_follower_recv() {
    if (UNKNOWN_ADDR == STATE.clusterhead_addr) {
        return RC::NoClusterhead;
    }
    // Every CH sends acks at the last slot of its downstream window.
    uint8_t ch_slot = STATE.upstream_ack_slot;
    int16_t slot = link::active_slot();
    if (slot != ch_slot) {
        return RC::InvalidSlot;
    }
    // Listen on the clusterhead's CDMA code (slot 0 of virtual window)
    link::set_cdma_code(0);

    bool waiting_for_clusterhead_msg = true;
    do {
        uint32_t slot_duration_ms = MAC->slot_duration() * 1000;
        link::RC rc = link::recv(slot_duration_ms);
        if (link::RC::Ok != rc) {
            return (rc == link::RC::TimedOut) ? RC::NoData : RC::LinkError;
        }

        uint8_t *buf;
        size_t sz;
        while (link::RC::Ok == link::getmsg(&buf, &sz)) {
            Message *msg;
            if (!decode_msg(&msg, buf, sz) || msg == nullptr) {
                continue;
            }
            if (msg->hdr.type == MessageType::SendAcks) {
                SendAcks *acks_msg = &msg->body.acks;
                bool in_range = STATE.upstream_relative_slot < acks_msg->end &&
                                STATE.upstream_relative_slot >= acks_msg->start;
                if (in_range) {
                    ack_t *acks = reinterpret_cast<ack_t *>(
                        reinterpret_cast<uint8_t *>(msg) + sizeof(Header) +
                        sizeof(SendAcks));
                    STATE.acks[0] =
                        acks[STATE.upstream_relative_slot - acks_msg->start];
                }
                waiting_for_clusterhead_msg = false;
            } else if (msg->hdr.type == MessageType::CloseCluster) {
                if (msg->body.close.nomination != UNKNOWN_ADDR) {
                    STATE.clusterhead_addr = msg->body.close.nomination;
                } else {
                    STATE.clusterhead_addr = UNKNOWN_ADDR;
                    STATE.clusterhead_ring = UNKNOWN_RING;
                }
                waiting_for_clusterhead_msg = false;
            }
        }
    } while (waiting_for_clusterhead_msg && link::active_slot() == ch_slot);

    return waiting_for_clusterhead_msg ? RC::NoData : RC::Ok;
}

/**
 * Queue and flush CH management messages (acks + CloseCluster) on CDMA code 0.
 * Called by do_clusterhead_send at the last slot of the downstream window.
 */
static RC emit_ch_messages() {
    link::set_cdma_code(0);

    uint8_t ack_start = 0;
    uint8_t ack_end = 0;
    while (ack_start < STATE.slots_assigned) {
        RC rc = send_acks(ack_start, &ack_end);
        if (rc != RC::Ok) {
            break;
        }
        ack_start = ack_end;
    }

    bool renominate = announce_clusterhead();
    close_cluster(renominate);

    return flush();
}

RC do_clusterhead_send() {
    uint8_t vtdma_window = vtdma_window_size();
    uint8_t ch_slot = STATE.downstream_start_slot + vtdma_window - 1;
    int16_t slot = link::active_slot();
    if (slot != ch_slot) {
        return RC::InvalidSlot;
    }
    return emit_ch_messages();
}

RC do_clusterhead_recv() {
    int16_t slot = link::active_slot();
    uint8_t vtdma_window = vtdma_window_size();
    uint8_t last_slot = STATE.downstream_start_slot + vtdma_window - 1;
    if (slot <= STATE.downstream_start_slot || slot >= last_slot) {
        return RC::InvalidSlot;
    }
    // Determine which follower's slot this is
    uint8_t relative = slot - STATE.downstream_start_slot;
    if (relative >= STATE.slots_assigned) {
        return RC::InvalidSlot;
    }
    // Set CDMA code to match the follower's code for this slot
    link::set_cdma_code(relative);

    // TODO: This should use time until next slot rather than blindly trusting
    // we fall right on the slot boundary
    uint32_t slot_duration_ms = MAC->slot_duration() * 1000;
    link::RC rc = link::recv(slot_duration_ms);
    if (link::RC::Ok != rc) {
        return (rc == link::RC::TimedOut) ? RC::NoData : RC::LinkError;
    }

    // Process received datagrams from the follower
    uint8_t *buf;
    size_t sz;
    while (link::RC::Ok == link::getmsg(&buf, &sz)) {
        Message *msg;
        if (!decode_msg(&msg, buf, sz) || msg == nullptr) {
            continue;
        }
        if (msg->hdr.type == MessageType::Datagram) {
            // Update ACK for this slot with the latest sequence number
            STATE.acks[relative] = msg->hdr.sequence;
            // Re-queue the entire message for forwarding to our own clusterhead
            link::sendmsg(buf, sz);
        }
    }
    return RC::Ok;
}

}  // namespace multihop
}  // namespace net
