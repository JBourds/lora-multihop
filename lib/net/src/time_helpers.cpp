#include "time_helpers.h"

#include <stdint.h>

#ifdef SIMULATE
#include <fcntl.h>
#include <unistd.h>

#include "arduino_stubs.h"
#else
#include <Arduino.h>
#include <RTClib.h>
#include <TimeLib.h>
RTC_DS3231 RTC;
bool INITIALIZED = false;
#endif

#ifdef SIMULATE
static ssize_t uint_from_file(const char *path) {
    char buf[32];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening file %s.\n", path);
        return -1;
    }
    ssize_t nread = read(fd, buf, sizeof(buf));
    close(fd);
    if (nread < 0) {
        fprintf(stderr, "Error reading from file %s.\n", path);
        return -2;
    }

    buf[nread] = '\0';
    char *nptr = NULL;
    uint64_t val = strtoull(buf, &nptr, 10);
    return static_cast<ssize_t>(val);
}
#endif

namespace net {
namespace time {

MsDeadline::MsDeadline(uint32_t duration_ms) {
    this->start = millis();
    this->deadline = this->start + duration_ms;
    this->overflows = this->deadline < this->start;
}

uint32_t MsDeadline::millis_until() {
    uint32_t now = millis();
    if (this->overflows) {
        uint32_t time_until_wraparound =
            now < this->start ? 0 : UINT32_MAX - now;
        uint32_t time_until_deadline =
            now < this->deadline
                ? (now > this->deadline ? 0 : this->deadline - now)
                : this->deadline;
        return time_until_wraparound + time_until_deadline;
    } else {
        // wrapped around, but the timer has finished
        if (now < this->start) {
            return 0;
        }
        return now > this->deadline ? 0 : this->deadline - now;
    }
}

bool MsDeadline::reached() {
    uint32_t now = millis();
    if (this->overflows) {
        return now > this->deadline && now <= this->start;
    } else {
        return now > this->deadline;
    }
}

#ifdef SIMULATE
uint32_t now() { return uint_from_file(NEXUS_ROOT "/ctl.time/s"); }
#else
uint32_t now() {
    if (!INITIALIZED) {
        INITIALIZED = RTC.begin();
    }

    DateTime now = RTC.now();
    uint32_t t = now.unixtime();
    return t;
}
#endif

#ifdef SIMULATE
void set(uint32_t t) {
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)t);
    int fd = open(NEXUS_ROOT "/ctl.time/s", O_WRONLY);
    if (fd < 0) {
        return;
    }
    write(fd, buf, len);
    close(fd);
}
#else
void set(uint32_t t) {
    DateTime dt = DateTime(t);
    RTC.adjust(dt);
}
#endif

void sleep_seconds(uint32_t seconds) {
    for (uint32_t i = 0; i < seconds; ++i) {
        delay(1000);
    }
}

void sleep_until(uint32_t point) {
    while (now() < point) {
        delay(1);
    }
}
}  // namespace time
}  // namespace net
