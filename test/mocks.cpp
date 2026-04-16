#include "mocks.h"

#include <stdint.h>

// -- Controllable millis stub --

static uint64_t g_mock_millis = 0;

void set_mock_millis(uint64_t value) { g_mock_millis = value; }

// Arduino time stubs (declared in time_stub.h)
uint64_t millis() { return g_mock_millis; }
uint64_t micros() { return g_mock_millis * 1000; }
void delay(uint64_t) {}
void delayMicroseconds(uint64_t) {}

// -- Extern constants from multihop.cpp (not linked in tests) --

namespace net {
namespace multihop {
extern const uint8_t GATEWAY_RING = 0;
extern const uint8_t UNKNOWN_RING = 255;
}  // namespace multihop

// -- Extern constant from link_layer.cpp (not linked in tests) --
namespace link {
extern const uint8_t DEFAULT_CODE = 0;
}  // namespace link
}  // namespace net
