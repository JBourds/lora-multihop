#ifdef SIMULATE

#define UNUSED(v) (void)(v)

#include "spi_stub.h"

#include <stddef.h>
#include <stdint.h>

SPIClass SPI;

void SPIClass::begin() {
#ifdef TRACE_MOCKS
    std::cout << "[SPI] begin()" << std::endl;
#endif
}

void SPIClass::end() {
#ifdef TRACE_MOCKS
    std::cout << "[SPI] end()" << std::endl;
#endif
}

void SPIClass::beginTransaction(const Settings& s) {
    inTransaction = true;
#ifdef TRACE_MOCKS
    std::cout << "[SPI] beginTransaction(clock=" << s.clock
              << ", bitOrder=" << s.bitOrder << ", dataMode=" << s.dataMode
              << ")" << std::endl;
#else
    UNUSED(s);
#endif
}

void SPIClass::endTransaction() {
    inTransaction = false;
#ifdef TRACE_MOCKS
    std::cout << "[SPI] endTransaction()" << std::endl;
#endif
}

uint8_t SPIClass::transfer(uint8_t data) {
#ifdef TRACE_MOCKS
    std::cout << "[SPI] transfer(" << data << ")" << std::endl;
#else
    UNUSED(data);
#endif
    return data;  // echo back
}

void SPIClass::transfer(void* buf, size_t count) {
#ifdef TRACE_MOCKS
    uint8_t* b = reinterpret_cast<uint8_t*>(buf);
    std::cout << "[SPI] transfer(buffer of size " << count << "): ";
    for (size_t i = 0; i < count; ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << std::endl;
#else
    UNUSED(buf);
    UNUSED(count);
#endif
}
#endif
