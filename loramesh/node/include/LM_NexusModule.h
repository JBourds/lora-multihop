// LM_NexusModule -- implements LM_Module using Nexus channel file I/O.
// Replaces the SPI-based radio (SX1276 etc.) with read/write to the
// simulator's FUSE channel files.
#pragma once

#include "modules/LM_Module.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class LM_NexusModule : public LM_Module {
public:
    // channel_path: path to the Nexus channel file (e.g., /tmp/nexus/lora)
    explicit LM_NexusModule(const std::string& channel_path);
    // Construct with pre-opened file descriptors (for opening before FreeRTOS)
    // rd_fd for reading (subscriber), wr_fd for writing (publisher)
    // channel_dir: path to the ctl directory (e.g., /nexus/ctl.lora_sf7)
    //   for reading rssi and snr control files
    LM_NexusModule(int rd_fd, int wr_fd, const std::string& channel_dir);
    ~LM_NexusModule() override;

    // LM_Module interface
    int16_t begin(float freq, float bw, uint8_t sf, uint8_t cr,
                  uint8_t syncWord, int8_t power,
                  int16_t preambleLength) override;

    int16_t receive(uint8_t* data, size_t len) override;
    int16_t startReceive() override;
    int16_t scanChannel() override;
    int16_t startChannelScan() override;
    int16_t standby() override;
    void reset() override;
    int16_t setCRC(bool crc) override;
    size_t getPacketLength() override;
    float getRSSI() override;
    float getSNR() override;
    int16_t readData(uint8_t* buffer, size_t numBytes) override;
    int16_t transmit(uint8_t* buffer, size_t length) override;
    uint32_t getTimeOnAir(size_t length) override;

    void setDioActionForReceiving(void (*action)()) override;
    void setDioActionForReceivingTimeout(void (*action)()) override;
    void setDioActionForScanning(void (*action)()) override;
    void setDioActionForScanningTimeout(void (*action)()) override;
    void clearDioActions() override;

    int16_t setFrequency(float freq) override;
    int16_t setBandwidth(float bw) override;
    int16_t setSpreadingFactor(uint8_t sf) override;
    int16_t setCodingRate(uint8_t cr) override;
    int16_t setSyncWord(uint8_t syncWord) override;
    int16_t setOutputPower(int8_t power) override;
    int16_t setPreambleLength(int16_t preambleLength) override;
    int16_t setGain(uint8_t gain) override;
    int16_t setOutputPower(int8_t power, int8_t useRfo) override;

private:
    std::string channel_path_;
    std::string rssi_path_;   // e.g. /nexus/ctl.lora_sf7/rssi
    std::string snr_path_;    // e.g. /nexus/ctl.lora_sf7/snr
    int channel_fd_;   // read fd
    int channel_wr_fd_; // write fd (may be same as read fd)

    // LoRa parameters (for time-on-air calculation)
    float freq_;
    float bw_;
    uint8_t sf_;
    uint8_t cr_;

    // Last received packet buffer
    uint8_t rx_buf_[256];
    size_t rx_len_;

    // DIO callback (simulates hardware interrupt on packet arrival)
    void (*dio_rx_action_)();
    std::atomic<bool> polling_active_;
    TaskHandle_t poll_task_;

    // Polling thread that reads from the channel file and fires the
    // DIO callback when a packet arrives.
    static void pollTask(void* param);

    // Lazy-open write fd from the calling thread to work around Nexus
    // FUSE using TID (not TGID) for permission checks.
    bool ensureWriteFd();
};
