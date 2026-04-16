#ifdef SIMULATE
#include "wire_stub.h"

#define UNUSED(v) (void)(v)

#include <stddef.h>
#include <stdint.h>

WireClass Wire;

void WireClass::begin() {
#ifdef TRACE_MOCKS
    std::cout << "[Wire] begin()" << std::endl;
#endif
}

void WireClass::begin(uint8_t address) {
#ifdef TRACE_MOCKS
    std::cout << "[Wire] begin(slave address=" << address << ")" << std::endl;
#else
    UNUSED(address);
#endif
}

void WireClass::beginTransmission(uint8_t address) {
    current_address = address;
#ifdef TRACE_MOCKS
    std::cout << "[Wire] beginTransmission(address=" << address << ")"
              << std::endl;
#else
    UNUSED(address);
#endif
}

uint8_t WireClass::endTransmission() {
#ifdef TRACE_MOCKS
    std::cout << "[Wire] endTransmission()" << std::endl;
#endif
    return 0;
}

uint8_t WireClass::requestFrom(uint8_t address, size_t quantity) {
#ifdef TRACE_MOCKS
    std::cout << "[Wire] requestFrom(address=" << address
              << ", quantity=" << quantity << ")" << std::endl;
#else
    UNUSED(address);
#endif

    // fill buffer with dummy data (0..quantity-1)
    for (size_t i = 0; i < quantity; ++i) {
        buf.push((uint8_t)i);
    }
    return static_cast<uint8_t>(quantity);
}

size_t WireClass::write(uint8_t data) {
#ifdef TRACE_MOCKS
    std::cout << "[Wire] write(" << data << ")" << std::endl;
#else
    UNUSED(data);
#endif
    return 1;
}

size_t WireClass::write(const uint8_t* data, size_t quantity) {
#ifdef TRACE_MOCKS
    std::cout << "[Wire] write(buffer of size " << quantity << "): ";
    for (size_t i = 0; i < quantity; ++i) {
        std::cout << data[i] << " ";
    }
    std::cout << std::endl;
#else
    UNUSED(data);
#endif
    return quantity;
}

int WireClass::available() { return (int)buf.size(); }

int WireClass::read() {
    if (buf.empty()) return -1;
    int val = buf.front();
    buf.pop();
    return val;
}

int WireClass::peek() {
    if (buf.empty()) return -1;
    return buf.front();
}
#endif
