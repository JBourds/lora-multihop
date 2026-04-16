#pragma once
#include <stdint.h>

namespace net {
namespace time {

struct MsDeadline {
    uint32_t start;
    uint32_t deadline;
    bool overflows;

    MsDeadline(uint32_t duration_ms);

    /**
     * @returns (bool): True if the deadline has been reached, false otherwise.
     */
    bool reached();

    /**
     * @returns (uint32_t): Milliseconds until the deadline, or 0 if it has been
     * reached.
     */
    uint32_t millis_until();
};

uint32_t now();
void set(uint32_t);
void sleep_seconds(uint32_t seconds);
void sleep_until(uint32_t point);
}  // namespace time
}  // namespace net
