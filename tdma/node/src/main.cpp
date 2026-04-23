#ifdef SIMULATE
#include <arduino_stubs.h>
#include <stdio.h>
#include <unistd.h>
#else
#include <Wire.h>
#endif

#include <stdint.h>
#include <string.h>

#include "LoraRadio.h"
#include "time_helpers.h"

#ifndef DEVICE_ID
uint8_t DEVICE_ID = 0;
#endif

#ifndef NODE_COUNT
#define NODE_COUNT 3
#endif

// Each slot is long enough for a human audience to follow in wall-clock
// playback (time_dilation=1.0 -> 1 sim-second per real-second).
#define SLOT_SECONDS 2
#define SUPERFRAME_SECONDS (NODE_COUNT * SLOT_SECONDS)

// Anchor time so every node agrees on which slot is "now" the moment it
// starts. Arbitrary choice; midnight 2026-01-01 UTC.
#define EPOCH_S 1767225600UL

#define RX_POLL_MS 25
#define MSG_BUF_SZ 128

static uint8_t rx_buf[MSG_BUF_SZ];

static void log_line(const char *s) {
    // setbuf(stdout, NULL) in main() makes stdout unbuffered, so fputs lands
    // in the trace file immediately -- no std::cout block-buffering surprise.
    fputs(s, stdout);
    fputc('\n', stdout);
}

static uint32_t slot_index(uint32_t now_s) {
    // Seconds since the shared epoch, folded into one superframe.
    uint32_t offset = (now_s - EPOCH_S) % SUPERFRAME_SECONDS;
    return offset / SLOT_SECONDS;
}

static uint32_t next_slot_boundary(uint32_t now_s) {
    uint32_t elapsed = now_s - EPOCH_S;
    uint32_t current_slot_start = (elapsed / SLOT_SECONDS) * SLOT_SECONDS;
    return EPOCH_S + current_slot_start + SLOT_SECONDS;
}

int main(int, char *argv[]) {
    DEVICE_ID = static_cast<uint8_t>(std::stoul(argv[1]));

    // Unbuffered stdio so the trace shows events as they happen.
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // Synchronise simulated wall clock across all nodes before we read it.
    net::time::set(EPOCH_S);

    if (lora::init() != lora::RC::Okay) {
        char line[64];
        snprintf(line, sizeof(line), "[node %u] lora init failed", DEVICE_ID);
        log_line(line);
        return 1;
    }

    {
        char line[96];
        const char *role = (DEVICE_ID == 0) ? "gateway" : "peripheral";
        snprintf(line, sizeof(line),
                 "[node %u] ready as %s, slot=%u/%d, superframe=%ds",
                 DEVICE_ID, role, DEVICE_ID, NODE_COUNT, SUPERFRAME_SECONDS);
        log_line(line);
    }

    uint32_t last_slot_seen = 0xFFFFFFFFu;
    bool transmitted_this_slot = false;

    while (true) {
        uint32_t now = net::time::now();
        uint32_t slot = slot_index(now);

        if (slot != last_slot_seen) {
            last_slot_seen = slot;
            transmitted_this_slot = false;
            char line[96];
            snprintf(line, sizeof(line), "[t=%lu] node %u sees slot %lu begin",
                     (unsigned long)(now - EPOCH_S), DEVICE_ID,
                     (unsigned long)slot);
            log_line(line);
        }

        if (slot == DEVICE_ID) {
            if (!transmitted_this_slot) {
                char msg[MSG_BUF_SZ];
                int n = snprintf(msg, sizeof(msg),
                                 "hello from node %u at t=%lu", DEVICE_ID,
                                 (unsigned long)(now - EPOCH_S));
                lora::RC rc = lora::send(reinterpret_cast<uint8_t *>(msg),
                                         static_cast<size_t>(n));
                char line[160];
                if (rc == lora::RC::Okay) {
                    snprintf(line, sizeof(line),
                             "[t=%lu] node %u TX (slot %u): %s",
                             (unsigned long)(now - EPOCH_S), DEVICE_ID,
                             DEVICE_ID, msg);
                } else {
                    snprintf(line, sizeof(line),
                             "[t=%lu] node %u TX failed rc=%d",
                             (unsigned long)(now - EPOCH_S), DEVICE_ID,
                             static_cast<int>(rc));
                }
                log_line(line);
                transmitted_this_slot = true;
            }
            // Wait out the rest of our slot without slamming the kernel.
            uint32_t boundary = next_slot_boundary(now);
            while (net::time::now() < boundary) {
                delay(50);
            }
        } else {
            // Listen phase: try to pull a packet with a short timeout.
            uint8_t len = MSG_BUF_SZ - 1;
            lora::RC rc = lora::wait_recv(rx_buf, len, RX_POLL_MS);
            if (rc == lora::RC::Okay && len > 0) {
                rx_buf[len] = '\0';
                char line[192];
                snprintf(line, sizeof(line),
                         "[t=%lu] node %u RX (slot %lu): %s",
                         (unsigned long)(net::time::now() - EPOCH_S),
                         DEVICE_ID, (unsigned long)slot,
                         reinterpret_cast<const char *>(rx_buf));
                log_line(line);
            }
        }
    }
}
