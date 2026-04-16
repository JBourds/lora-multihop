#ifdef SIMULATE
#include <arduino_stubs.h>
#include <stdio.h>
#include <unistd.h>
#else
#include <Wire.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "LoraRadio.h"
#include "link_layer.h"
#include "multihop.h"
#include "time_helpers.h"

#ifndef DEVICE_ID
uint8_t DEVICE_ID = 0;
#endif

// Superframe parameters
// Each superframe has N slots; slot 0 is advertising, the rest are virtual TDMA
// windows for clusterhead-follower communication.
// Window length = slot_s * slot_count; frequency must be >= window length.
#define SUPERFRAME_SLOT_S 3
#define VTDMA_WINDOW 5
#define REUSE_DISTANCE 3
#define SUPERFRAME_SLOT_COUNT (1 + REUSE_DISTANCE * VTDMA_WINDOW)
#define SUPERFRAME_FREQUENCY_S (SUPERFRAME_SLOT_S * SUPERFRAME_SLOT_COUNT + 5)
#define GUARD_PERIOD_S 0
#define MSG_BUF_SIZE 255

// Multihop slot allocation
#define MAX_SLOTS (SUPERFRAME_SLOT_COUNT)
static uint32_t acks_buf[MAX_SLOTS];
static net::Address slots_buf[MAX_SLOTS];

// Buffers for TX/RX
static uint8_t tx_buf[MSG_BUF_SIZE];
static uint8_t rx_buf[MSG_BUF_SIZE];

// Simulated sensor data counter
static uint32_t data_seq = 0;

void setup();
void loop();

void setup() {
    // Seed PRNG per-node so clusterhead elections are independent
    srand(DEVICE_ID * 1000 + 42);

    Wire.begin();
    Serial.begin(9600);
    while (!Serial) {
        delay(10);
    }

    Serial.print("[Node ");
    Serial.print(DEVICE_ID);
    Serial.println("] Starting multihop protocol");

    net::NodeParams node = {
        .address = DEVICE_ID,
        .time_s = net::time::now(),
    };

    net::MacParams mac = {
        .superframe_slot_s = SUPERFRAME_SLOT_S,
        .superframe_frequency_s = SUPERFRAME_FREQUENCY_S,
        .superframe_slot_count = SUPERFRAME_SLOT_COUNT,
        .guard_period_s = GUARD_PERIOD_S,
        .reuse_distance = REUSE_DISTANCE,
    };

    net::LinkParams link = {
        .msg_max_bytes = MSG_BUF_SIZE - sizeof(net::link::FrameHeader) -
                         SUPERFRAME_SLOT_COUNT,
        .tx_buf = tx_buf,
        .tx_sz = MSG_BUF_SIZE,
        .rx_buf = rx_buf,
        .rx_sz = MSG_BUF_SIZE,
    };

    lora::RC lora_rc = lora::init();
    if (lora_rc != lora::RC::Okay) {
        Serial.print("[Node ");
        Serial.print(DEVICE_ID);
        Serial.print("] Lora init failed (RC = ");
        Serial.print(static_cast<uint8_t>(lora_rc));
        Serial.println(")");
        return;
    }

    // Clusterhead probability: gateway always volunteers (255), others at 50%
    uint8_t ch_prob = (DEVICE_ID == 0) ? 255 : 128;

    net::multihop::RC rc = net::multihop::init(&node, &mac, &link, acks_buf,
                                               slots_buf, MAX_SLOTS, ch_prob);
    if (rc != net::multihop::RC::Ok) {
        Serial.print("[Node ");
        Serial.print(DEVICE_ID);
        Serial.print("] Multihop init failed (RC = ");
        Serial.print(static_cast<uint8_t>(rc));
        Serial.println(")");
        return;
    }

    Serial.print("[Node ");
    Serial.print(DEVICE_ID);
    Serial.println("] Multihop initialized successfully.");
}

void loop() {
    net::multihop::MultihopState *state = net::multihop::get_state();
    if (!state) {
        delay(1000);
        return;
    }

    // Wait for the advertising slot
    uint32_t adv_time, adv_offset;
    net::multihop::RC rc =
        net::multihop::advertisement_slot(&adv_time, &adv_offset);

    if (rc == net::multihop::RC::Ok || rc == net::multihop::RC::NotInWindow) {
        Serial.print("[Node ");
        Serial.print(DEVICE_ID);
        Serial.print("] Sleeping until adv slot (offset=");
        Serial.print(adv_offset);
        Serial.println("s)");
        net::time::sleep_until(adv_time);
    } else {
        Serial.print("[Node ");
        Serial.print(DEVICE_ID);
        Serial.print("] advertisement_slot RC=");
        Serial.println(static_cast<uint8_t>(rc));
        delay(1000);
        return;
    }

    // Phase 1: Advertising (neighbor discovery + clusterhead election)
    rc = net::multihop::do_advertise();
    if (rc != net::multihop::RC::Ok) {
        Serial.print("[Node ");
        Serial.print(DEVICE_ID);
        Serial.print("] Advertise failed (RC = ");
        Serial.print(static_cast<uint8_t>(rc));
        Serial.println(")");
        return;
    }

    Serial.print("[Node ");
    Serial.print(DEVICE_ID);
    Serial.print("] Ring=");
    Serial.print(state->ring);
    Serial.print(" Role=");
    Serial.print(static_cast<uint8_t>(state->role));
    Serial.print(" CH=");
    Serial.print(state->clusterhead_addr);
    Serial.print(" up_start=");
    Serial.print(state->upstream_start_slot);
    Serial.print(" down_start=");
    Serial.print(state->downstream_start_slot);
    Serial.print(" ack_slot=");
    Serial.println(state->upstream_ack_slot);

    // Inverted superframe: deeper rings go first, gateway last.
    // For relays, downstream window < upstream window (no wraparound assumed).
    //
    // Temporal order for a relay:
    //   1. CH-recv in downstream window (receive from deeper followers)
    //   2. CH-send acks at last slot of downstream window
    //   3. Follower-send in upstream window (forward data + own data)
    //   4. Follower-recv at upstream CH's ack slot

    // Phase 2: CH-recv + CH-send (downstream window)
    if (state->acts_as_clusterhead()) {
        for (uint8_t slot = 1; slot < VTDMA_WINDOW - 1; ++slot) {
            uint32_t slot_time, slot_offset;
            uint8_t abs_slot = state->downstream_start_slot + slot;
            rc = net::multihop::epoch_and_offset(abs_slot, &slot_time,
                                                 &slot_offset);
            if (rc != net::multihop::RC::Ok &&
                rc != net::multihop::RC::NotInWindow) {
                break;
            }
            net::time::sleep_until(slot_time);
            net::multihop::do_clusterhead_recv();
        }

        // All CHs (gateway and relay) send acks at last slot of downstream
        // window.
        uint32_t ch_time, ch_offset;
        uint8_t ch_slot = state->downstream_start_slot + VTDMA_WINDOW - 1;
        rc = net::multihop::epoch_and_offset(ch_slot, &ch_time, &ch_offset);
        if (rc == net::multihop::RC::Ok ||
            rc == net::multihop::RC::NotInWindow) {
            net::time::sleep_until(ch_time);
            net::multihop::do_clusterhead_send();
        }
    }

    // Phase 3: Follower-send (upstream window)
    // Only enter follower path if we actually joined a cluster this superframe.
    if (state->acts_as_follower() &&
        state->clusterhead_addr != net::UNKNOWN_ADDR) {
        uint32_t my_time, my_offset;
        rc = net::multihop::my_slot(&my_time, &my_offset);
        if (rc == net::multihop::RC::Ok ||
            rc == net::multihop::RC::NotInWindow) {
            net::time::sleep_until(my_time);

            // Queue and send follower data on our assigned CDMA code
            char payload[64];
            snprintf(payload, sizeof(payload), "DATA:%u:seq=%lu", DEVICE_ID,
                     (unsigned long)data_seq++);
            net::multihop::DatagramIterator iter(
                reinterpret_cast<uint8_t *>(payload), strlen(payload),
                net::link::mtu() - sizeof(net::multihop::Header) -
                    sizeof(net::multihop::Datagram));
            net::multihop::send_datagram(&iter);
            net::multihop::do_follower_send();
        }

        // Phase 4: Listen for ACKs from our CH at last slot of its window
        uint32_t recv_time, recv_offset;
        rc = net::multihop::epoch_and_offset(state->upstream_ack_slot,
                                             &recv_time, &recv_offset);
        if (rc == net::multihop::RC::Ok ||
            rc == net::multihop::RC::NotInWindow) {
            net::time::sleep_until(recv_time);
            net::multihop::do_follower_recv();
        }
    }

    // Brief delay before next superframe
    delay(100);
}

#ifdef SIMULATE
int main(int, char *argv[]) {
    DEVICE_ID = std::stoul(argv[1]);
    std::cout << "Node: " << DEVICE_ID << std::endl;
    setup();
    while (1) {
        loop();
    }
}
#endif
