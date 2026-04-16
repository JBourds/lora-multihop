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

static uint64_t uint_from_file(const char* path) {
    char buf[32];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening file %s.", path);
        return 1;
    }
    ssize_t nread = read(fd, buf, sizeof(buf));
    if (nread < 0) {
        fprintf(stderr, "Error reading from file %s.", path);
        return 2;
    }
    close(fd);

    buf[nread] = '\0';
    char* nptr = NULL;
    uint64_t val = strtoull(buf, &nptr, 10);
    return val;
}

void delay(uint64_t ms) {
    uint64_t now = millis();
    uint64_t deadline = now + ms;
    while (millis() <= deadline) {
    }
}

void delayMicroseconds(uint64_t us) {
    uint64_t now = micros();
    uint64_t deadline = now + us;
    while (micros() <= deadline) {
    }
}

uint64_t millis() { return uint_from_file(MILLIS_PATH); }

uint64_t micros() { return uint_from_file(MICROS_PATH); }

#endif
