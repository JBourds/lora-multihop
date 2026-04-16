
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef SIMULATE
#include <queue>
#endif

class WireClass {
   public:
    void begin();
    void begin(uint8_t address);  // optional for slave
    void beginTransmission(uint8_t address);
    uint8_t endTransmission();
    uint8_t requestFrom(uint8_t address, size_t quantity);

    size_t write(uint8_t data);
    size_t write(const uint8_t* data, size_t quantity);

    int available();
    int read();
    int peek();

   private:
#ifdef SIMULATE
    std::queue<uint8_t> buf;
#endif
    uint8_t current_address = 0;
};

// Global instance
extern WireClass Wire;
