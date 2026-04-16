#include "esp32_shims.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ---------------------------------------------------------------------------
// Nexus virtual time via control files
// ---------------------------------------------------------------------------
static const char* NEXUS_ROOT_ENV = nullptr;
static char millis_path[256];
static char micros_path[256];
static bool time_paths_init = false;

static void init_time_paths() {
    if (time_paths_init) {
        return;
    }
    const char* root = NEXUS_ROOT_ENV;
    if (nullptr == root) {
        root = getenv("HOME");
        if (nullptr == root) {
            root = "/tmp";
        }
        static char buf[256];
        snprintf(buf, sizeof(buf), "%s/nexus", root);
        root = buf;
    }
    snprintf(millis_path, sizeof(millis_path), "%s/ctl.elapsed/ms", root);
    snprintf(micros_path, sizeof(micros_path), "%s/ctl.elapsed/us", root);
    time_paths_init = true;
}

void set_nexus_root(const char* root) {
    NEXUS_ROOT_ENV = root;
    time_paths_init = false;
    init_time_paths();
}

static uint64_t uint_from_file(const char* path) {
    char buf[32];
    int fd = open(path, O_RDONLY);
    if (0 > fd) {
        return 0;
    }
    ssize_t nread = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (0 >= nread) {
        return 0;
    }
    buf[nread] = '\0';
    return strtoull(buf, nullptr, 10);
}

// ---------------------------------------------------------------------------
// millis() -- Nexus virtual milliseconds since simulation start
// ---------------------------------------------------------------------------
unsigned long millis() {
    init_time_paths();
    return static_cast<unsigned long>(uint_from_file(millis_path));
}

// ---------------------------------------------------------------------------
// random(low, high) -- Arduino-style random
// ---------------------------------------------------------------------------
long random(long howsmall, long howbig) {
    if (howsmall >= howbig) {
        return howsmall;
    }
    return howsmall + (std::rand() % (howbig - howsmall));
}

// ---------------------------------------------------------------------------
// delay(ms) -- spin on Nexus virtual time (matches arduino_stubs pattern)
// ---------------------------------------------------------------------------
void delay(unsigned long ms) {
    unsigned long now = millis();
    unsigned long deadline = now + ms;
    while (millis() <= deadline) {
        // Spin; Nexus advances virtual time independently
    }
}

// ---------------------------------------------------------------------------
// getFreeHeap() -- report available memory (approximate on Linux)
// ---------------------------------------------------------------------------
size_t getFreeHeap() {
    return 512 * 1024;
}

// ---------------------------------------------------------------------------
// FreeRTOS idle hook -- prevent 100% CPU spin
// ---------------------------------------------------------------------------
extern "C" void vApplicationIdleHook(void) {
    usleep(15000);
}

// ---------------------------------------------------------------------------
// WiFi shim -- fake MAC address from node ID
// ---------------------------------------------------------------------------
static uint16_t g_node_address = 0;

void WiFiShim::setNodeAddress(uint16_t addr) {
    g_node_address = addr;
}

std::string WiFiShim::macAddress() {
    char buf[18];
    snprintf(buf, sizeof(buf), "00:00:00:00:%02X:%02X",
             (g_node_address >> 8) & 0xFF, g_node_address & 0xFF);
    return std::string(buf);
}

void WiFiShim::macAddress(uint8_t* mac) {
    mac[0] = 0x00;
    mac[1] = 0x00;
    mac[2] = 0x00;
    mac[3] = 0x00;
    mac[4] = (g_node_address >> 8) & 0xFF;
    mac[5] = g_node_address & 0xFF;
}

// Global WiFi instance
WiFiShim WiFi;

// ---------------------------------------------------------------------------
// LoRaMesher globals (declared extern in BuildOptions.h)
// ---------------------------------------------------------------------------
const char* LM_TAG = "LoRaMesher";
const char* LM_VERSION = "0.0.11-nexus";
