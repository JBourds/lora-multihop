// LoRaMesher on Nexus simulator -- entry point.
//
// Usage: ./main <node_address> [nexus_root]
//   node_address: uint16 node ID (passed by Nexus runner via run_args)
//   nexus_root:   path to Nexus FUSE mount (default: ~/nexus)

#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "LM_NexusModule.h"
#include "LoraMesher.h"
#include "esp32_shims.h"

// ---------------------------------------------------------------------------
// Application receive callback
// ---------------------------------------------------------------------------
static TaskHandle_t app_recv_handle = nullptr;

struct DataPayload {
    uint32_t counter;
    uint16_t src_addr;
};

static void appRecvTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        auto& mesh = LoraMesher::getInstance();
        while (0 < mesh.getReceivedQueueSize()) {
            auto* pkt = mesh.getNextAppPacket<DataPayload>();
            if (nullptr != pkt) {
                DataPayload* data =
                    reinterpret_cast<DataPayload*>(pkt->payload);
                printf(
                    "[APP] Received counter=%u from node=0x%04X "
                    "(via=0x%04X)\n",
                    data->counter, data->src_addr, pkt->src);
                mesh.deletePacket(pkt);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Periodic data sender task
// ---------------------------------------------------------------------------
static uint16_t g_node_addr = 0;

static void dataSendTask(void*) {
    uint32_t counter = 0;
    // Stagger first send by node address to reduce initial collisions
    vTaskDelay((1000 + g_node_addr * 500) / portTICK_PERIOD_MS);

    for (;;) {
        DataPayload payload = {
            .counter = counter++,
            .src_addr = g_node_addr,
        };
        printf("[DATA] Node 0x%04X sending counter=%u\n", g_node_addr,
               payload.counter);

        auto& mesh = LoraMesher::getInstance();
        mesh.createPacketAndSend(BROADCAST_ADDR, &payload, 1);

        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

// ---------------------------------------------------------------------------
// Main task (runs after FreeRTOS scheduler starts)
// ---------------------------------------------------------------------------
static std::string g_nexus_root;
static int g_channel_rd_fd = -1;  // opened before scheduler starts
static int g_channel_wr_fd = -1;

static void mainTask(void*) {
    printf("[main] Node 0x%04X starting LoRaMesher\n", g_node_addr);

    // Set WiFi shim address (used for node ID / local address derivation)
    WiFi.setNodeAddress(g_node_addr);

    // Create nexus radio module with pre-opened fds
    std::string ctl_dir = g_nexus_root + "/ctl.lora_sf7";
    auto* nexus_radio =
        new LM_NexusModule(g_channel_rd_fd, g_channel_wr_fd, ctl_dir);

    // Inject the radio module before calling begin()
    auto& mesh = LoraMesher::getInstance();
    mesh.setRadio(nexus_radio);

    // Configure and start LoRaMesher
    LoraMesher::LoraMesherConfig config;
    config.loraCs = 0;
    config.loraIrq = 0;
    config.loraRst = 0;
    config.freq = 915.0f;
    config.bw = 125.0f;
    config.sf = 7;
    config.cr = 5;

    printf("[main] Calling LoraMesher::begin()\n");
    mesh.begin(config);

    // Register the app receive callback
    xTaskCreate(appRecvTask, "app_recv", 4096, nullptr, 1, &app_recv_handle);
    mesh.setReceiveAppDataTaskHandle(app_recv_handle);

    printf("[main] Calling LoraMesher::start()\n");
    mesh.start();

    // Start periodic data sender
    xTaskCreate(dataSendTask, "data_send", 4096, nullptr, 2, nullptr);

    printf("[main] Node 0x%04X fully initialized\n", g_node_addr);

    // Keep main task alive (heartbeat)
    for (;;) {
        printf("[main] Node 0x%04X alive, heap=%zu, routes=%u\n", g_node_addr,
               getFreeHeap(), mesh.routingTableSize());
        vTaskDelay(15000 / portTICK_PERIOD_MS);
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (2 > argc) {
        fprintf(stderr, "Usage: %s <node_address> [nexus_root]\n", argv[0]);
        return 1;
    }

    g_node_addr = static_cast<uint16_t>(atoi(argv[1]));

    if (3 <= argc) {
        g_nexus_root = argv[2];
    } else {
        const char* home = getenv("HOME");
        g_nexus_root = std::string(home ? home : "/tmp") + "/nexus";
    }

    printf("[boot] LoRaMesher-Nexus node=%u root=%s\n", g_node_addr,
           g_nexus_root.c_str());

    // Set nexus root for virtual time control files
    set_nexus_root(g_nexus_root.c_str());

    // Open channel file BEFORE FreeRTOS scheduler starts.
    // Nexus exclusive channels may require separate read/write fds.
    std::string channel_path = g_nexus_root + "/ctl.lora_sf7/channel";
    g_channel_rd_fd = open(channel_path.c_str(), O_RDONLY);
    g_channel_wr_fd = open(channel_path.c_str(), O_WRONLY);
    if (0 > g_channel_rd_fd || 0 > g_channel_wr_fd) {
        fprintf(stderr,
                "[boot] Failed to open channel: %s (rd=%d wr=%d errno=%d)\n",
                channel_path.c_str(), g_channel_rd_fd, g_channel_wr_fd, errno);
    } else {
        printf("[boot] Opened channel: %s (rd=%d wr=%d pid=%d)\n",
               channel_path.c_str(), g_channel_rd_fd, g_channel_wr_fd,
               static_cast<int>(getpid()));
    }

    xTaskCreate(mainTask, "main_task", 8192, nullptr, 3, nullptr);
    vTaskStartScheduler();

    return 0;
}
