#pragma once

#include <stdint.h>

// Control the value returned by the millis() stub
void set_mock_millis(uint64_t value);
