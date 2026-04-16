// ESP32-specific API shims for POSIX/Nexus.
// Provides stubs for xTaskCreatePinnedToCore, spinlock critical sections,
// esp_log, and other ESP-IDF APIs that LoRaMesher uses.
#pragma once

#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Nexus virtual time root
// ---------------------------------------------------------------------------
void set_nexus_root(const char* root);

// ---------------------------------------------------------------------------
// Task pinning shim (POSIX has no cores)
// ---------------------------------------------------------------------------
#define xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core) \
    xTaskCreate((fn), (name), (stack), (param), (prio), (handle))

// ---------------------------------------------------------------------------
// Spinlock shims (ESP32 uses portMUX_TYPE for ISR-safe critical sections)
// ---------------------------------------------------------------------------
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
// ESP32 taskENTER_CRITICAL takes a portMUX_TYPE*; POSIX FreeRTOS takes no arg.
// We undef the POSIX version and redefine to accept (and ignore) the mux arg.
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL(...)  portENTER_CRITICAL()
#define taskEXIT_CRITICAL(...)   portEXIT_CRITICAL()

// ESP32 allows portYIELD_FROM_ISR() with zero arguments (unconditional yield).
// The POSIX port requires one argument.  Override to accept zero or one arg.
#undef portYIELD_FROM_ISR
#define portYIELD_FROM_ISR(...) vPortYield()

// ---------------------------------------------------------------------------
// ESP logging shims
// ---------------------------------------------------------------------------
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

// esp_log.h compatibility
#define esp_log_level_set(tag, level) ((void)0)
#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

// ---------------------------------------------------------------------------
// Heap shims
// ---------------------------------------------------------------------------
#define esp_get_free_heap_size() getFreeHeap()
#define heap_caps_get_free_size(caps) getFreeHeap()
#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_INTERNAL 0

// ---------------------------------------------------------------------------
// WiFi shims (WiFiService uses WiFi.macAddress())
// ---------------------------------------------------------------------------
#include <string>

class WiFiShim {
public:
    // Returns a fake MAC based on node address (set in main.cpp)
    static std::string macAddress();
    static void macAddress(uint8_t* mac);
    static void setNodeAddress(uint16_t addr);
};

// LoRaMesher references `WiFi.macAddress()` in WiFiService.
// Use a global instance so `WiFi.macAddress(mac)` compiles.
extern WiFiShim WiFi;
