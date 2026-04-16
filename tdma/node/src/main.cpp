#ifdef SIMULATE
#include <arduino_stubs.h>
#include <stdio.h>
#include <unistd.h>
#else
#include <Wire.h>
#endif

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "LoraRadio.h"
#include "link_layer.h"
#include "time_helpers.h"

#ifndef DEVICE_ID
uint8_t DEVICE_ID = 0;
#endif

// TDMA parameters
#define SUPERFRAME_SLOT_S 2
#define SUPERFRAME_FREQUENCY_S 7
#define SUPERFRAME_SLOT_COUNT 3
#define GUARD_PERIOD_S 0
#define MSG_BUF_SIZE 255

// Buffers for TX/RX
static uint8_t tx_buf[MSG_BUF_SIZE];
static uint8_t rx_buf[MSG_BUF_SIZE];

void setup();
void loop();

void setup() {
    Wire.begin();
    Serial.begin(9600);
    while (!Serial) {
        delay(10);
    }

    Serial.print("Device ID: ");
    Serial.println(DEVICE_ID);

#ifdef SIMULATE
    setbuf(stdout, NULL);
#endif

    // TDMA initialization
    net::NodeParams node = {
        .address = DEVICE_ID,
        .time_s = net::time::now(),
    };

    net::MacParams mac = {
        .superframe_slot_s = SUPERFRAME_SLOT_S,
        .superframe_frequency_s = SUPERFRAME_FREQUENCY_S,
        .superframe_slot_count = SUPERFRAME_SLOT_COUNT,
        .guard_period_s = GUARD_PERIOD_S,
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
        Serial.print("Lora init failed (RC = ");
        Serial.print(static_cast<uint8_t>(lora_rc));
        Serial.println(")");
    } else {
        Serial.println("Lora initialized successfully.");
    }
    net::link::RC net_rc = net::link::init(&node, &mac, &link);
    if (net_rc != net::link::RC::Ok) {
        Serial.print("TDMA init failed (RC = ");
        Serial.print(static_cast<uint8_t>(net_rc));
        Serial.println(")");
    } else {
        Serial.println("TDMA initialized successfully.");
    }
    // January 1st, 2026 at 12am
    net::time::set(1767225600);
}

void loop() {
    // Queue a message
    char msg[64];
    snprintf(msg, sizeof(msg), "Hello from node %d at %lu", DEVICE_ID,
             (unsigned long)net::time::now());
    net::link::RC rc = net::link::sendmsg((uint8_t *)msg, strlen(msg));
    if (rc != net::link::RC::Ok) {
        Serial.print("sendmsg failed: ");
        Serial.println(static_cast<uint8_t>(rc));
    }

    // Receive other node messages until our slot
    while (net::link::active_slot() != DEVICE_ID) {
        net::link::RC rc = net::link::recv(SUPERFRAME_SLOT_S);
        if (rc == net::link::RC::Ok) {
            uint8_t *buf;
            size_t sz;
            while (net::link::getmsg(&buf, &sz) == net::link::RC::Ok) {
                Serial.print("Got message (");
                Serial.print(sz);
                Serial.print(" bytes): ");
                Serial.println(reinterpret_cast<char *>(buf));
            }
        }
    }

    // Our turn now, flush messages
    rc = net::link::flush();
    if (rc != net::link::RC::Ok) {
        if (rc == net::link::RC::NotYourTurn) {
            Serial.println("Waiting for my slot...");
        } else {
            Serial.print("flush failed: ");
            Serial.println(static_cast<uint8_t>(rc));
        }
    } else {
        Serial.print("Message sent: ");
        Serial.println(msg);
    }
    delay(500);
}

#ifdef SIMULATE
int main(int, char *argv[]) {
    DEVICE_ID = std::stoul(argv[1]);
    setup();
    while (1) {
        loop();
    }
}
#endif
