#ifdef SIMULATE
#include "time_stub.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char* MILLIS_PATH = NEXUS_ROOT "/ctl.elapsed/ms";
const char* MICROS_PATH = NEXUS_ROOT "/ctl.elapsed/us";
const char* SLEEP_REL_MS_PATH = NEXUS_ROOT "/ctl.sleep.relative/ms";
const char* SLEEP_REL_US_PATH = NEXUS_ROOT "/ctl.sleep.relative/us";

// Cached fds for every Nexus control file this stub touches. Each entry
// is opened lazily on first use and reused for the lifetime of the
// process — without caching, every millis()/micros()/delay() call would
// pay an open+close per invocation, which dominates the CPU budget
// under tight cgroup throttling. The sleep_rel_*_fd writes block at the
// FUSE layer until simulated time advances by the requested amount, so
// delay() now costs one write() per call instead of a busy-poll loop.
static int millis_fd = -1;
static int micros_fd = -1;
static int sleep_rel_ms_fd = -1;
static int sleep_rel_us_fd = -1;

static int open_path(const char* path, int flags) {
    int fd = open(path, flags);
    if (fd < 0) {
        fprintf(stderr, "Error opening file %s.", path);
    }
    return fd;
}

static uint64_t uint_from_fd(int fd) {
    char buf[32];
    ssize_t nread = read(fd, buf, sizeof(buf));
    if (nread < 0) {
        fprintf(stderr, "Error reading elapsed fd.");
        return 2;
    }
    buf[nread] = '\0';
    char* nptr = NULL;
    return strtoull(buf, &nptr, 10);
}

static void sleep_write(int fd, uint64_t value) {
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (0 >= n) {
        fprintf(stderr, "snprintf failed in sleep_write.");
        return;
    }
    ssize_t written = write(fd, buf, (size_t)n);
    if (0 > written) {
        fprintf(stderr, "Error writing sleep fd.");
    }
}

void delay(uint64_t ms) {
    if (0 == ms) {
        return;
    }
    if (0 > sleep_rel_ms_fd) {
        sleep_rel_ms_fd = open_path(SLEEP_REL_MS_PATH, O_WRONLY);
        if (0 > sleep_rel_ms_fd) return;
    }
    sleep_write(sleep_rel_ms_fd, ms);
}

void delayMicroseconds(uint64_t us) {
    if (0 == us) {
        return;
    }
    if (0 > sleep_rel_us_fd) {
        sleep_rel_us_fd = open_path(SLEEP_REL_US_PATH, O_WRONLY);
        if (0 > sleep_rel_us_fd) return;
    }
    sleep_write(sleep_rel_us_fd, us);
}

uint64_t millis() {
    if (0 > millis_fd) {
        millis_fd = open_path(MILLIS_PATH, O_RDONLY);
        if (0 > millis_fd) return 1;
    }
    return uint_from_fd(millis_fd);
}

uint64_t micros() {
    if (0 > micros_fd) {
        micros_fd = open_path(MICROS_PATH, O_RDONLY);
        if (0 > micros_fd) return 1;
    }
    return uint_from_fd(micros_fd);
}

#endif
