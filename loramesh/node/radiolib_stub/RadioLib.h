// Minimal RadioLib stub for compiling LoRaMesher on Linux.
// Only provides the types and constants that LoRaMesher headers reference.
// The actual radio is replaced by LM_NexusModule.
#pragma once

#include <cstdint>
#include <cstddef>

// RadioLib status codes used by LM_Module interface and LoRaMesher
#define RADIOLIB_ERR_NONE                0
#define RADIOLIB_ERR_UNKNOWN            -1
#define RADIOLIB_ERR_SPI_WRITE_FAILED   -2
#define RADIOLIB_ERR_RX_TIMEOUT         -6
#define RADIOLIB_NC                     0xFF

// Stub for RadioLibHal (referenced by EspHal and LoraMesherConfig)
class RadioLibHal {
public:
    virtual ~RadioLibHal() {}
    virtual void pinMode(uint32_t, uint32_t) {}
    virtual void digitalWrite(uint32_t, uint32_t) {}
    virtual uint32_t digitalRead(uint32_t) { return 0; }
    virtual void attachInterrupt(uint32_t, void (*)(), uint32_t) {}
    virtual void detachInterrupt(uint32_t) {}
    virtual void delay(unsigned long) {}
    virtual unsigned long millis() { return 0; }
    virtual void spiBegin() {}
    virtual void spiEnd() {}
    virtual void spiBeginTransaction() {}
    virtual void spiEndTransaction() {}
    virtual void spiTransfer(uint8_t*, size_t, uint8_t*) {}
    virtual void yield() {}
    virtual void tone(uint32_t, unsigned int, unsigned long) {}
    virtual void noTone(uint32_t) {}
};

// Stub Module class (RadioLib's hardware abstraction)
class Module {
public:
    Module() {}
    Module(uint8_t, uint8_t, uint8_t, uint8_t) {}
    Module(RadioLibHal*, uint8_t, uint8_t, uint8_t, uint8_t) {}
};
