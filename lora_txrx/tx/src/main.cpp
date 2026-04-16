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
    static uint32_t counter = 0;
    size_t nwritten =
        snprintf(msg, lora::PACKET_MAX_SIZE_BYTES, "TX[%lu]", counter++);
    msg[nwritten++] = '\0';
    RC rc = lora::send(reinterpret_cast<uint8_t*>(msg), nwritten);
    if (rc == lora::RC::Okay) {
        Serial.println(reinterpret_cast<char*>(msg));
    } else {
        error("Failed to send message");
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
