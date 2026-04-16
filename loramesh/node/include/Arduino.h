// Arduino compatibility shim for LoRaMesher on Linux/Nexus.
// BuildOptions.h does `#ifdef ARDUINO` and includes "Arduino.h".
// This file provides the minimal surface area that path expects.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <cmath>

// Arduino-style type aliases
using String = std::string;
using byte = uint8_t;

// GPIO constants (unused on Nexus, but referenced by config structs)
#define LOW   0x0
#define HIGH  0x1
#define INPUT 0x01
#define OUTPUT 0x03
#define RISING 0x01
#define FALLING 0x02

// F() macro (no-op on Linux)
#define F(string_literal) (string_literal)

// Stub SPI class (LoRaMesherConfig references SPIClass* under ARDUINO)
class SPIClass {
public:
    SPIClass() {}
};

// Forward declarations for functions defined in esp32_shims.cpp
unsigned long millis();
long random(long howsmall, long howbig);
inline void randomSeed(unsigned long seed) { srand(static_cast<unsigned>(seed)); }
size_t getFreeHeap();
void delay(unsigned long ms);

// FreeRTOS headers (LoRaMesher uses these heavily)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ESP32 shims
#include "esp32_shims.h"
