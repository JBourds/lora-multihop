
#pragma once
#include <stddef.h>
#include <stdint.h>

class SPIClass {
   public:
    void begin();
    void end();

    struct Settings {
        uint32_t clock;
        uint8_t bitOrder;
        uint8_t dataMode;

        Settings(uint32_t c = 4000000, uint8_t b = 0, uint8_t d = 0)
            : clock(c), bitOrder(b), dataMode(d) {}
    };

    void beginTransaction(const Settings& settings);
    void endTransaction();

    uint8_t transfer(uint8_t data);
    void transfer(void* buf, size_t count);

   private:
    bool inTransaction = false;
};

// Global instance
extern SPIClass SPI;
