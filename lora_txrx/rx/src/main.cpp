#include "LoraRadio.h"

#ifdef SIMULATE
#include <iostream>

#include "arduino_stubs.h"
#else
#include <Arduino.h>
#include <Wire.h>
#endif

using lora::RC;

void error(const char* msg) {
    Serial.println("ERROR: ");
    Serial.println(msg);
#ifdef SIMULATE
    exit(EXIT_FAILURE);
#else
    while (true) {
    }
#endif
}

void setup() {
    Wire.begin();
    Serial.begin(9600);
    while (!Serial) {
    }
    delay(50);

    RC rc = lora::init();
    switch (rc) {
        case RC::InitFailed:
            error("Failed to initialized RF95");
            break;
        case RC::SetFrequencyFailed:
            error("Failed to set frequency");
            break;
        default:
            break;
    }
}

void loop() {
    char msg[lora::PACKET_MAX_SIZE_BYTES];
    uint8_t len = lora::PACKET_MAX_SIZE_BYTES;
    RC rc = lora::wait_recv(reinterpret_cast<uint8_t*>(msg), len, 5000);
    if (rc == lora::RC::Okay) {
        Serial.println(msg);
    } else if (rc == lora::RC::TimedOut) {
        Serial.println("Timed out");
    } else {
        error("Failed to receive message");
    }
}

#ifdef SIMULATE
int main() {
    setup();
    while (true) {
        loop();
    }
}
#endif
