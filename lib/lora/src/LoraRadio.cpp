#include "LoraRadio.h"

#include "arduino_stubs.h"

#ifdef SIMULATE
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <iostream>

#include "stdio.h"
#endif

namespace lora {

static bool INITIALIZED = false;
static int16_t LAST_RSSI = 1;

#ifdef SIMULATE
#ifndef NEXUS_ROOT
#error "\"NEXUS_ROOT\" must be defined by simulation to locate file path"
#endif
static int RF95;
static SF SPREAD = SF::Default;
#else
static const uint8_t LORA_CS = 10;
static const uint8_t LORA_INT = 2;
static const uint8_t LORA_RST = 9;
static const float LORA_FREQUENCY = 915.0;

static RH_RF95 RF95(LORA_CS, LORA_INT);
#endif

bool is_active() { return INITIALIZED; }

#ifdef SIMULATE
int* get() { return INITIALIZED ? &RF95 : nullptr; }
const char* sf_string(SF sf) {
    switch (sf) {
        case SF::SF6:
            return NEXUS_ROOT "/lora_sf6/channel";
        case SF::SF7:
            return NEXUS_ROOT "/lora_sf7/channel";
        case SF::SF8:
            return NEXUS_ROOT "/lora_sf8/channel";
        case SF::SF9:
            return NEXUS_ROOT "/lora_sf9/channel";
        case SF::SF10:
            return NEXUS_ROOT "/lora_sf10/channel";
        case SF::SF11:
            return NEXUS_ROOT "/lora_sf11/channel";
        case SF::SF12:
            return NEXUS_ROOT "/lora_sf12/channel";
        default:
            fprintf(stderr, "Unable to determine lora channel");
            exit(EXIT_FAILURE);
    }
}
#else
RH_RF95* get() { return INITIALIZED ? &RF95 : nullptr; }
#endif

int16_t last_rssi() { return LAST_RSSI; }

#ifdef SIMULATE
RC init() {
    const char* path = sf_string(SPREAD);
    RF95 = open(path, O_RDWR);
    printf("%s\n", path);
    if (RF95 == -1) {
        return RC::InitFailed;
    }
    INITIALIZED = true;
    return RC::Okay;
}
#else
RC init() {
    pinMode(3, OUTPUT);
    pinMode(5, OUTPUT);
    digitalWrite(3, HIGH);
    digitalWrite(5, HIGH);

    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, HIGH);

    digitalWrite(LORA_RST, LOW);
    delay(10);
    digitalWrite(LORA_RST, HIGH);
    delay(10);

    if (!RF95.init()) {
        return RC::InitFailed;
    }
    if (!RF95.setFrequency(LORA_FREQUENCY)) {
        return RC::SetFrequencyFailed;
    }

    INITIALIZED = true;

    return RC::Okay;
}
#endif

#ifdef SIMULATE
RC deinit() {
    if (!INITIALIZED) {
        return RC::NotInit;
    }
    if (close(RF95) == -1) {
        return RC::DeinitFailed;
    }
    INITIALIZED = false;
    return RC::Okay;
}
#else
RC deinit() {
    if (!INITIALIZED) {
        return RC::NotInit;
    }
    // Forfeit SPI line
    pinMode(LORA_CS, INPUT);
    INITIALIZED = false;
    return RC::Okay;
}
#endif

#ifdef SIMULATE
RC send(const uint8_t buf[], size_t sz) {
    RC rc = RC::Okay;
    if (!is_active()) {
        rc = init();
    }
    ssize_t nwritten = write(RF95, buf, sz);
    if (nwritten < 0 || nwritten != static_cast<ssize_t>(sz)) {
        rc = RC::SendFailed;
    }
    return rc;
}
#else
RC send(const uint8_t buf[], size_t sz) {
    RC rc = RC::Okay;
    if (!is_active()) {
        rc = init();
    }

    RH_RF95* rf95 = lora::get();

    if (rc == RC::Okay && !rf95->send(buf, sz)) {
        rc = RC::SendFailed;
    }
    if (rc == RC::Okay && !rf95->waitPacketSent()) {
        rc = RC::TimedOut;
    }
    return rc;
}
#endif

#ifdef SIMULATE
RC wait_recv(uint8_t buf[], uint8_t& len, uint32_t timeout_ms) {
    RC rc = RC::Okay;
    if (!is_active()) {
        rc = init();
        if (rc != RC::Okay) {
            return rc;
        }
    }
    uint32_t end_ms = millis() + timeout_ms;
    do {
        ssize_t nread = read(RF95, buf, len);
        if (nread > 0) {
            len = nread;
            return RC::Okay;
        }
    } while (millis() < end_ms);
    return RC::TimedOut;
}
#else
RC wait_recv(uint8_t buf[], uint8_t& len, uint32_t timeout_ms) {
    RC rc = RC::Okay;
    if (!is_active()) {
        rc = init();
    }
    if (timeout_ms == 0) {
        RF95.waitAvailable();
    } else {
        RF95.waitAvailableTimeout(timeout_ms);
    }

    if (rc == RC::Okay && !RF95.recv(buf, &len)) {
        rc = RC::RecvFailed;
    }
    return rc;
}
#endif

#ifdef SIMULATE

RC set_spreading_factor(SF sf) {
    if (is_active()) {
        deinit();
    }
    SPREAD = sf;
    return init();
}
#else
RC set_spreading_factor(SF sf) {
    RF95.setSpreadingFactor(static_cast<uint8_t>(sf));
}
#endif

}  // namespace lora
