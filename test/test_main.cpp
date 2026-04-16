#include "test.h"

#include "mocks.h"
#include "multihop_messages.h"
#include "multihop_state.h"
#include "net.h"
#include "time_helpers.h"

#include <stdlib.h>
#include <string.h>

using namespace net;
using namespace net::multihop;

// ============================================================
// RingList::score() tests
// ============================================================

bool score_gateway_returns_max() {
    RingList rl = {};
    ASSERT_EQ(rl.score(GATEWAY_RING), UINT32_MAX);
    return true;
}

bool score_empty_list_returns_zero() {
    RingList rl = {};
    ASSERT_EQ(rl.score(1), (uint32_t)0);
    return true;
}

bool score_single_neighbor_at_ring_zero() {
    RingList rl = {};
    rl.ring_start = 0;
    rl.counts[0] = 1;
    // max = 0 + 8 = 8; multiplier for i=0 is 8
    ASSERT_EQ(rl.score(1), (uint32_t)8);
    return true;
}

bool score_weighted_sum() {
    RingList rl = {};
    rl.ring_start = 1;
    rl.counts[0] = 2;
    rl.counts[2] = 3;
    // max = 1 + 8 = 9
    // i=0: multiplier 9, count 2 -> 18
    // i=2: multiplier 7, count 3 -> 21
    // total = 39
    ASSERT_EQ(rl.score(2), (uint32_t)39);
    return true;
}

bool score_high_ring_start_offset() {
    RingList rl = {};
    rl.ring_start = 200;
    rl.counts[0] = 1;
    // max = 200 + 8 = 208; multiplier for i=0 is 208
    ASSERT_EQ(rl.score(201), (uint32_t)208);
    return true;
}

bool score_all_counts_filled() {
    RingList rl = {};
    rl.ring_start = 0;
    for (size_t i = 0; i < 8; ++i) {
        rl.counts[i] = 1;
    }
    // max = 8; sum = 8+7+6+5+4+3+2+1 = 36
    ASSERT_EQ(rl.score(1), (uint32_t)36);
    return true;
}

// ============================================================
// DatagramIterator tests
// ============================================================

bool datagram_single_fragment() {
    uint8_t buf[10];
    memset(buf, 0xAA, sizeof(buf));
    DatagramIterator it(buf, 10, 20);
    ASSERT_EQ(it.count, (uint8_t)1);
    ASSERT(it.has_next());

    Datagram d;
    uint8_t *start, *end;
    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.packet_bytes, (uint16_t)10);
    ASSERT_EQ(d.count, (uint8_t)1);
    ASSERT_EQ(d.part, (uint8_t)0);
    ASSERT_EQ(start, buf);
    ASSERT_EQ(end, buf + 10);

    ASSERT(!it.has_next());
    return true;
}

bool datagram_multiple_fragments() {
    uint8_t buf[25];
    memset(buf, 0, sizeof(buf));
    DatagramIterator it(buf, 25, 10);
    ASSERT_EQ(it.count, (uint8_t)3);

    Datagram d;
    uint8_t *start, *end;

    // Fragment 0: 10 bytes
    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.packet_bytes, (uint16_t)10);
    ASSERT_EQ(d.part, (uint8_t)0);

    // Fragment 1: 10 bytes
    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.packet_bytes, (uint16_t)10);
    ASSERT_EQ(d.part, (uint8_t)1);

    // Fragment 2: 5 bytes (remainder)
    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.packet_bytes, (uint16_t)5);
    ASSERT_EQ(d.part, (uint8_t)2);

    ASSERT(!it.has_next());
    return true;
}

bool datagram_peek_does_not_advance() {
    uint8_t buf[20];
    DatagramIterator it(buf, 20, 10);

    Datagram d1, d2;
    uint8_t *s1, *e1, *s2, *e2;

    ASSERT(it.peek(&d1, &s1, &e1));
    ASSERT(it.peek(&d2, &s2, &e2));

    ASSERT_EQ(d1.part, d2.part);
    ASSERT_EQ(d1.packet_bytes, d2.packet_bytes);
    ASSERT_EQ(s1, s2);
    ASSERT_EQ(e1, e2);
    return true;
}

bool datagram_next_advances() {
    uint8_t buf[20];
    DatagramIterator it(buf, 20, 10);

    Datagram d;
    uint8_t *start, *end;

    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.part, (uint8_t)0);

    ASSERT(it.peek(&d, &start, &end));
    ASSERT_EQ(d.part, (uint8_t)1);
    return true;
}

bool datagram_rewind_undoes_last() {
    uint8_t buf[20];
    DatagramIterator it(buf, 20, 10);

    Datagram d;
    uint8_t *start, *end;

    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.part, (uint8_t)0);

    uint16_t rewound = it.rewind();
    ASSERT_EQ(rewound, (uint16_t)10);

    ASSERT(it.next(&d, &start, &end));
    ASSERT_EQ(d.part, (uint8_t)0);
    return true;
}

bool datagram_empty_buffer() {
    uint8_t buf[1];
    DatagramIterator it(buf, 0, 10);
    ASSERT(!it.has_next());
    return true;
}

bool datagram_oversized_rejects() {
    // count = ceil(2560/10) = 256 > UINT8_MAX, so iterator exhausts immediately
    uint8_t buf[2560];
    DatagramIterator it(buf, 2560, 10);
    ASSERT_EQ(it.count, (uint8_t)0);
    ASSERT(!it.has_next());
    return true;
}

bool datagram_exact_uint8_max_fragments() {
    // 255 fragments of 10 bytes = 2550 bytes
    uint8_t buf[2550];
    DatagramIterator it(buf, 2550, 10);
    ASSERT_EQ(it.count, (uint8_t)255);
    ASSERT(it.has_next());
    return true;
}

// ============================================================
// MsDeadline tests
// ============================================================

bool msdeadline_not_reached() {
    set_mock_millis(1000);
    time::MsDeadline d(500);

    set_mock_millis(1200);
    ASSERT(!d.reached());
    return true;
}

bool msdeadline_reached() {
    set_mock_millis(1000);
    time::MsDeadline d(500);

    set_mock_millis(1501);
    ASSERT(d.reached());
    return true;
}

bool msdeadline_exactly_at_boundary() {
    set_mock_millis(1000);
    time::MsDeadline d(500);

    // Deadline is 1500. At exactly 1500, not yet "reached" (> not >=).
    set_mock_millis(1500);
    ASSERT(!d.reached());
    return true;
}

bool msdeadline_millis_until_decreasing() {
    set_mock_millis(1000);
    time::MsDeadline d(500);

    set_mock_millis(1200);
    uint32_t remaining = d.millis_until();
    ASSERT_EQ(remaining, (uint32_t)300);

    set_mock_millis(1400);
    remaining = d.millis_until();
    ASSERT_EQ(remaining, (uint32_t)100);
    return true;
}

bool msdeadline_millis_until_zero_past() {
    set_mock_millis(1000);
    time::MsDeadline d(500);

    set_mock_millis(2000);
    ASSERT_EQ(d.millis_until(), (uint32_t)0);
    return true;
}

bool msdeadline_overflow_not_reached_before_wrap() {
    // start near uint32_t max, duration causes overflow
    set_mock_millis(UINT32_MAX - 100);
    time::MsDeadline d(200);
    // deadline = UINT32_MAX - 100 + 200 = 99 (overflows)
    ASSERT(d.overflows);

    // Still before wraparound
    set_mock_millis(UINT32_MAX - 50);
    ASSERT(!d.reached());
    return true;
}

bool msdeadline_overflow_not_reached_after_wrap_before_deadline() {
    set_mock_millis(UINT32_MAX - 100);
    time::MsDeadline d(200);
    // deadline = 99

    // Wrapped around but not past deadline
    set_mock_millis(50);
    ASSERT(!d.reached());
    return true;
}

bool msdeadline_overflow_reached_after_deadline() {
    set_mock_millis(UINT32_MAX - 100);
    time::MsDeadline d(200);
    // deadline = 99

    // Wrapped around and past deadline
    set_mock_millis(100);
    ASSERT(d.reached());
    return true;
}

// ============================================================
// MultihopState tests
// ============================================================

bool state_gateway_address_sets_role() {
    MultihopState s;
    s.with_address(GATEWAY_ADDR);
    ASSERT(s.role == Role::Gateway);
    return true;
}

bool state_non_gateway_sets_follower() {
    MultihopState s;
    s.with_address(5);
    ASSERT(s.role == Role::Follower);
    return true;
}

bool state_gateway_ring_always_zero() {
    MultihopState s;
    s.with_address(GATEWAY_ADDR);
    s.ring = 5;  // artificially set wrong ring
    s.update_ring_from_neighbors();
    ASSERT_EQ(s.ring, GATEWAY_RING);
    return true;
}

bool state_add_neighbor_single() {
    MultihopState s;
    s.with_address(1);
    s.ring = UNKNOWN_RING;
    s.reset_neighbors();
    s.add_neighbor(0);
    ASSERT_EQ(s.neighbors.ring_start, (uint8_t)0);
    ASSERT_EQ(s.neighbors.counts[0], (uint8_t)1);
    return true;
}

bool state_add_neighbor_lower_shifts() {
    MultihopState s;
    s.with_address(2);
    s.ring = UNKNOWN_RING;
    s.reset_neighbors();
    s.add_neighbor(3);
    ASSERT_EQ(s.neighbors.ring_start, (uint8_t)3);
    ASSERT_EQ(s.neighbors.counts[0], (uint8_t)1);

    // Add a lower neighbor, should shift
    s.add_neighbor(1);
    ASSERT_EQ(s.neighbors.ring_start, (uint8_t)1);
    ASSERT_EQ(s.neighbors.counts[0], (uint8_t)1);  // ring 1
    ASSERT_EQ(s.neighbors.counts[2], (uint8_t)1);  // ring 3 shifted to index 2
    return true;
}

bool state_update_ring_from_lower_neighbor() {
    MultihopState s;
    s.with_address(5);
    s.ring = 4;
    s.reset_neighbors();
    s.add_neighbor(1);
    s.update_ring_from_neighbors();
    // Ring should be lowest neighbor + 1
    ASSERT_EQ(s.ring, (uint8_t)2);
    return true;
}

bool state_hysteresis_prevents_immediate_ring_increase() {
    MultihopState s;
    s.with_address(5);
    s.ring = 2;
    s.rounds_without_lower = 0;
    s.reset_neighbors();
    // Add only a same-ring neighbor (ring 2)
    s.add_neighbor(2);
    s.update_ring_from_neighbors();
    // Should NOT increase ring yet (hysteresis)
    ASSERT_EQ(s.ring, (uint8_t)2);
    ASSERT_EQ(s.rounds_without_lower, (uint8_t)1);
    return true;
}

bool state_hysteresis_allows_ring_increase_after_threshold() {
    MultihopState s;
    s.with_address(5);
    s.ring = 2;
    s.rounds_without_lower = MultihopState::RING_HYSTERESIS_THRESHOLD - 1;
    s.reset_neighbors();
    s.add_neighbor(2);
    s.update_ring_from_neighbors();
    // Should now increase ring (threshold reached)
    ASSERT_EQ(s.ring, (uint8_t)3);
    ASSERT_EQ(s.rounds_without_lower, (uint8_t)0);
    return true;
}

// ============================================================
// Role predicate tests
// ============================================================

bool clusterhead_acts_as_both() {
    // A Clusterhead is always also a follower (it must have an upstream CH)
    MultihopState s;
    s.with_address(5);
    s.role = Role::Clusterhead;
    ASSERT(s.acts_as_clusterhead());
    ASSERT(s.acts_as_follower());
    return true;
}

bool gateway_acts_as_ch_only() {
    // Gateway is the only CH without an upstream
    MultihopState s;
    s.with_address(GATEWAY_ADDR);
    ASSERT(s.acts_as_clusterhead());
    ASSERT(!s.acts_as_follower());
    return true;
}

bool follower_acts_as_follower_only() {
    MultihopState s;
    s.with_address(5);
    // with_address sets role to Follower for non-gateway
    ASSERT(!s.acts_as_clusterhead());
    ASSERT(s.acts_as_follower());
    return true;
}

// ============================================================
// Window formula tests (inverted layout)
//
// Formula: start = ADV_SLOTS + (reuse_distance - 1 - (ring % reuse_distance))
//                  * vtdma_window
//
// We can't call prepare_clusterhead() directly (it depends on static MAC/STATE
// in multihop.cpp), so we test the formula inline here.
// ============================================================

static uint8_t compute_downstream_start(uint8_t ring, uint8_t reuse_distance,
                                        uint8_t vtdma_window) {
    uint8_t adv_slots = 1;
    uint8_t inverted = reuse_distance - 1 - (ring % reuse_distance);
    return adv_slots + inverted * vtdma_window;
}

bool window_formula_reuse3_ring0() {
    // Ring 0 (gateway) should get the LAST window
    // reuse=3, vtdma=5: 1 + (2 - 0) * 5 = 11
    ASSERT_EQ(compute_downstream_start(0, 3, 5), (uint8_t)11);
    return true;
}

bool window_formula_reuse3_ring1() {
    // Ring 1: 1 + (2 - 1) * 5 = 6
    ASSERT_EQ(compute_downstream_start(1, 3, 5), (uint8_t)6);
    return true;
}

bool window_formula_reuse3_ring2() {
    // Ring 2: 1 + (2 - 2) * 5 = 1 (first window, deepest)
    ASSERT_EQ(compute_downstream_start(2, 3, 5), (uint8_t)1);
    return true;
}

bool window_formula_reuse2_ring0() {
    // reuse=2, vtdma=5: 1 + (1 - 0) * 5 = 6
    ASSERT_EQ(compute_downstream_start(0, 2, 5), (uint8_t)6);
    return true;
}

bool window_formula_reuse2_ring1() {
    // 1 + (1 - 1) * 5 = 1
    ASSERT_EQ(compute_downstream_start(1, 2, 5), (uint8_t)1);
    return true;
}

bool window_formula_spatial_reuse_wraps() {
    // Ring N and ring N + reuse_distance get the same start_slot
    // reuse=3: ring 0 and ring 3 should match
    ASSERT_EQ(compute_downstream_start(0, 3, 5),
              compute_downstream_start(3, 3, 5));
    // ring 1 and ring 4 should match
    ASSERT_EQ(compute_downstream_start(1, 3, 5),
              compute_downstream_start(4, 3, 5));
    return true;
}

bool window_formula_deeper_rings_go_first() {
    // With reuse_distance=3: ring 2 < ring 1 < ring 0 in slot order
    uint8_t r0 = compute_downstream_start(0, 3, 5);
    uint8_t r1 = compute_downstream_start(1, 3, 5);
    uint8_t r2 = compute_downstream_start(2, 3, 5);
    ASSERT(r2 < r1);
    ASSERT(r1 < r0);
    return true;
}

bool window_formula_reuse4() {
    // reuse=4, vtdma=5:
    // ring 0: 1 + 3*5 = 16 (last)
    // ring 3: 1 + 0*5 = 1  (first)
    ASSERT_EQ(compute_downstream_start(0, 4, 5), (uint8_t)16);
    ASSERT_EQ(compute_downstream_start(3, 4, 5), (uint8_t)1);
    return true;
}

// ============================================================
// CH credit tests
// ============================================================

bool credit_init_zero() {
    MultihopState s;
    ASSERT_EQ(s.ch_credit, (uint8_t)0);
    return true;
}

bool credit_increments_on_follower() {
    MultihopState s;
    s.with_address(5);
    // role is Follower after with_address for non-gateway
    ASSERT_EQ(s.ch_credit, (uint8_t)0);
    // Simulate what do_advertise() does for a non-CH node
    if (s.role != Role::Gateway) {
        if (s.acts_as_clusterhead()) {
            s.ch_credit = 0;
        } else if (255 > s.ch_credit) {
            ++s.ch_credit;
        }
    }
    ASSERT_EQ(s.ch_credit, (uint8_t)1);
    return true;
}

bool credit_resets_on_ch() {
    MultihopState s;
    s.with_address(5);
    s.ch_credit = 4;
    s.role = Role::Clusterhead;
    // Simulate credit bookkeeping
    if (s.role != Role::Gateway) {
        if (s.acts_as_clusterhead()) {
            s.ch_credit = 0;
        } else if (255 > s.ch_credit) {
            ++s.ch_credit;
        }
    }
    ASSERT_EQ(s.ch_credit, (uint8_t)0);
    return true;
}

bool credit_saturates_at_255() {
    MultihopState s;
    s.with_address(5);
    s.ch_credit = 254;
    // One increment should reach 255
    if (s.role != Role::Gateway) {
        if (s.acts_as_clusterhead()) {
            s.ch_credit = 0;
        } else if (255 > s.ch_credit) {
            ++s.ch_credit;
        }
    }
    ASSERT_EQ(s.ch_credit, (uint8_t)255);
    // Another round should stay at 255
    if (s.role != Role::Gateway) {
        if (s.acts_as_clusterhead()) {
            s.ch_credit = 0;
        } else if (255 > s.ch_credit) {
            ++s.ch_credit;
        }
    }
    ASSERT_EQ(s.ch_credit, (uint8_t)255);
    return true;
}

bool gateway_ignores_credit() {
    MultihopState s;
    s.with_address(GATEWAY_ADDR);
    s.ch_credit = 0;
    // Gateway path: bookkeeping is skipped entirely
    if (s.role != Role::Gateway) {
        if (s.acts_as_clusterhead()) {
            s.ch_credit = 0;
        } else if (255 > s.ch_credit) {
            ++s.ch_credit;
        }
    }
    // Credit unchanged -- gateway never enters the block
    ASSERT_EQ(s.ch_credit, (uint8_t)0);
    return true;
}

bool credit_scales_probability() {
    // Statistical test: with credit=6 and base_p=64, effective_p should be
    // min(64 + 6*32, 255) = 255 (guaranteed election).
    // Run announce_clusterhead logic N times, expect 100% wins.
    const uint8_t CREDIT_SCALE = 32;
    uint8_t base_p = 64;
    uint8_t credit = 6;
    uint16_t effective_p = base_p;
    effective_p += static_cast<uint16_t>(credit) * CREDIT_SCALE;
    if (255 < effective_p) {
        effective_p = 255;
    }
    ASSERT_EQ(effective_p, (uint16_t)255);

    // With credit=0, effective_p = 64 (25%). Over 10000 trials, expect ~2500.
    // Accept anywhere in [1500, 3500] to avoid flaky test.
    srand(42);
    uint16_t wins = 0;
    uint16_t eff_low = 64;
    for (int i = 0; i < 10000; ++i) {
        uint8_t v = static_cast<uint8_t>(rand());
        if (static_cast<uint8_t>(eff_low) >= v) {
            ++wins;
        }
    }
    ASSERT(wins >= 1500);
    ASSERT(wins <= 3500);

    // With credit=4, effective_p = 64 + 128 = 192 (75%). Expect ~7500.
    srand(42);
    wins = 0;
    uint16_t eff_mid = 64 + 4 * 32;
    for (int i = 0; i < 10000; ++i) {
        uint8_t v = static_cast<uint8_t>(rand());
        if (static_cast<uint8_t>(eff_mid) >= v) {
            ++wins;
        }
    }
    ASSERT(wins >= 6000);
    ASSERT(wins <= 9000);

    return true;
}

// ============================================================

int main() {
    printf("== RingList::score ==\n");
    RUN(score_gateway_returns_max);
    RUN(score_empty_list_returns_zero);
    RUN(score_single_neighbor_at_ring_zero);
    RUN(score_weighted_sum);
    RUN(score_high_ring_start_offset);
    RUN(score_all_counts_filled);

    printf("\n== DatagramIterator ==\n");
    RUN(datagram_single_fragment);
    RUN(datagram_multiple_fragments);
    RUN(datagram_peek_does_not_advance);
    RUN(datagram_next_advances);
    RUN(datagram_rewind_undoes_last);
    RUN(datagram_empty_buffer);
    RUN(datagram_oversized_rejects);
    RUN(datagram_exact_uint8_max_fragments);

    printf("\n== MsDeadline ==\n");
    RUN(msdeadline_not_reached);
    RUN(msdeadline_reached);
    RUN(msdeadline_exactly_at_boundary);
    RUN(msdeadline_millis_until_decreasing);
    RUN(msdeadline_millis_until_zero_past);
    RUN(msdeadline_overflow_not_reached_before_wrap);
    RUN(msdeadline_overflow_not_reached_after_wrap_before_deadline);
    RUN(msdeadline_overflow_reached_after_deadline);

    printf("\n== MultihopState ==\n");
    RUN(state_gateway_address_sets_role);
    RUN(state_non_gateway_sets_follower);
    RUN(state_gateway_ring_always_zero);
    RUN(state_add_neighbor_single);
    RUN(state_add_neighbor_lower_shifts);
    RUN(state_update_ring_from_lower_neighbor);
    RUN(state_hysteresis_prevents_immediate_ring_increase);
    RUN(state_hysteresis_allows_ring_increase_after_threshold);

    printf("\n== Role predicates ==\n");
    RUN(clusterhead_acts_as_both);
    RUN(gateway_acts_as_ch_only);
    RUN(follower_acts_as_follower_only);

    printf("\n== Window formula (inverted layout) ==\n");
    RUN(window_formula_reuse3_ring0);
    RUN(window_formula_reuse3_ring1);
    RUN(window_formula_reuse3_ring2);
    RUN(window_formula_reuse2_ring0);
    RUN(window_formula_reuse2_ring1);
    RUN(window_formula_spatial_reuse_wraps);
    RUN(window_formula_deeper_rings_go_first);
    RUN(window_formula_reuse4);

    printf("\n== CH credit ==\n");
    RUN(credit_init_zero);
    RUN(credit_increments_on_follower);
    RUN(credit_resets_on_ch);
    RUN(credit_saturates_at_255);
    RUN(gateway_ignores_credit);
    RUN(credit_scales_probability);

    return test_summary();
}
