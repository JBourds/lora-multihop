#include "LM_NexusModule.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <cstdio>

// RadioLib status codes
#ifndef RADIOLIB_ERR_NONE
#define RADIOLIB_ERR_NONE 0
#endif

// Derive the channel directory from a channel file path.
// e.g. "/nexus/ctl.lora_sf7/channel" -> "/nexus/ctl.lora_sf7"
static std::string channel_dir(const std::string& channel_path) {
    auto pos = channel_path.rfind('/');
    if (std::string::npos != pos) {
        return channel_path.substr(0, pos);
    }
    return channel_path;
}

static float float_from_file(const std::string& path) {
    char buf[32];
    int fd = open(path.c_str(), O_RDONLY);
    if (0 > fd) {
        return 0.0f;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (0 >= n) {
        return 0.0f;
    }
    buf[n] = '\0';
    return strtof(buf, nullptr);
}

LM_NexusModule::LM_NexusModule(const std::string& channel_path)
    : channel_path_(channel_path),
      rssi_path_(channel_dir(channel_path) + "/rssi"),
      snr_path_(channel_dir(channel_path) + "/snr"),
      channel_fd_(-1),
      channel_wr_fd_(-1),
      freq_(915.0f),
      bw_(125.0f),
      sf_(7),
      cr_(5),
      rx_len_(0),
      dio_rx_action_(nullptr),
      polling_active_(false),
      poll_task_(nullptr) {
    memset(rx_buf_, 0, sizeof(rx_buf_));
}

LM_NexusModule::LM_NexusModule(int rd_fd, int wr_fd,
                               const std::string& ctl_dir)
    : channel_path_("<pre-opened>"),
      rssi_path_(ctl_dir + "/rssi"),
      snr_path_(ctl_dir + "/snr"),
      channel_fd_(rd_fd),
      channel_wr_fd_(wr_fd),
      freq_(915.0f),
      bw_(125.0f),
      sf_(7),
      cr_(5),
      rx_len_(0),
      dio_rx_action_(nullptr),
      polling_active_(false),
      poll_task_(nullptr) {
    memset(rx_buf_, 0, sizeof(rx_buf_));
}

LM_NexusModule::~LM_NexusModule() {
    polling_active_ = false;
    if (nullptr != poll_task_) {
        vTaskDelete(poll_task_);
        poll_task_ = nullptr;
    }
    if (0 <= channel_fd_) {
        close(channel_fd_);
    }
    if (0 <= channel_wr_fd_ && channel_wr_fd_ != channel_fd_) {
        close(channel_wr_fd_);
    }
}

int16_t LM_NexusModule::begin(float freq, float bw, uint8_t sf, uint8_t cr,
                               uint8_t /*syncWord*/, int8_t /*power*/,
                               int16_t /*preambleLength*/) {
    freq_ = freq;
    bw_ = bw;
    sf_ = sf;
    cr_ = cr;

    // If fd was provided at construction time, skip open.
    if (0 <= channel_fd_) {
        fprintf(stderr, "[LM_NexusModule] Using pre-opened fd=%d\n",
                channel_fd_);
        return RADIOLIB_ERR_NONE;
    }

    // Retry opening the channel file; the FUSE mount may not be ready
    // immediately when the process starts.
    for (int attempt = 0; attempt < 10; ++attempt) {
        channel_fd_ = open(channel_path_.c_str(), O_RDWR);
        if (0 <= channel_fd_) {
            fprintf(stderr, "[LM_NexusModule] Opened channel: %s (fd=%d)\n",
                    channel_path_.c_str(), channel_fd_);
            return RADIOLIB_ERR_NONE;
        }
        fprintf(stderr, "[LM_NexusModule] Waiting for channel: %s (attempt %d)\n",
                channel_path_.c_str(), attempt);
        usleep(500000);
    }
    fprintf(stderr, "[LM_NexusModule] Failed to open channel: %s\n",
            channel_path_.c_str());
    return -1;
}

// ---------------------------------------------------------------------------
// Receive path
// ---------------------------------------------------------------------------

int16_t LM_NexusModule::startReceive() {
    if (!polling_active_ && nullptr != dio_rx_action_) {
        polling_active_ = true;
        xTaskCreate(pollTask, "nexus_poll", 4096, this, 6, &poll_task_);
    }
    return RADIOLIB_ERR_NONE;
}

void LM_NexusModule::pollTask(void* param) {
    auto* self = static_cast<LM_NexusModule*>(param);
    uint8_t buf[256];
    while (self->polling_active_) {
        // Blocking read from Nexus channel file.
        // Nexus delivers complete packets; read() returns one message.
        ssize_t n = read(self->channel_fd_, buf, sizeof(buf));
        if (0 < n) {
            // Store in rx buffer
            self->rx_len_ = static_cast<size_t>(n);
            memcpy(self->rx_buf_, buf, self->rx_len_);
            // Fire DIO callback (simulates hardware interrupt)
            if (nullptr != self->dio_rx_action_) {
                self->dio_rx_action_();
            }
        } else {
            // No data or error; yield to avoid busy-spin
            vTaskDelay(1);
        }
    }
    vTaskDelete(nullptr);
}

int16_t LM_NexusModule::receive(uint8_t* data, size_t len) {
    if (0 == rx_len_) {
        return -1;
    }
    size_t copy = (len < rx_len_) ? len : rx_len_;
    memcpy(data, rx_buf_, copy);
    rx_len_ = 0;
    return RADIOLIB_ERR_NONE;
}

int16_t LM_NexusModule::readData(uint8_t* buffer, size_t numBytes) {
    return receive(buffer, numBytes);
}

size_t LM_NexusModule::getPacketLength() {
    return rx_len_;
}

// ---------------------------------------------------------------------------
// Transmit path
// ---------------------------------------------------------------------------

int16_t LM_NexusModule::transmit(uint8_t* buffer, size_t length) {
    int wr_fd = (0 <= channel_wr_fd_) ? channel_wr_fd_ : channel_fd_;
    if (0 > wr_fd) {
        return -1;
    }
    ssize_t n = write(wr_fd, buffer, length);
    if (n != static_cast<ssize_t>(length)) {
        // Log PID/TID to diagnose FUSE permission issues
        fprintf(stderr, "[LM_NexusModule] write() returned %zd for %zu bytes "
                        "(errno=%d: %s, pid=%d, tid=%d)\n",
                n, length, errno, strerror(errno),
                static_cast<int>(getpid()),
                static_cast<int>(gettid()));
        return -1;
    }
    return RADIOLIB_ERR_NONE;
}

// ---------------------------------------------------------------------------
// Time on air -- LoRa modulation formula
// ---------------------------------------------------------------------------

uint32_t LM_NexusModule::getTimeOnAir(size_t length) {
    // Symbol time: Tsym = 2^SF / BW (in ms, BW in kHz)
    float tsym = pow(2.0f, sf_) / bw_;
    // Preamble time (assume 8 + 4.25 symbols)
    float t_preamble = (8.0f + 4.25f) * tsym;
    // Payload symbols (simplified formula)
    float payload_symbols =
        8.0f +
        fmaxf(ceilf((8.0f * length - 4.0f * sf_ + 28.0f) /
                     (4.0f * (sf_ - 2))) *
                   cr_,
               0.0f);
    float t_payload = payload_symbols * tsym;
    return static_cast<uint32_t>(t_preamble + t_payload);
}

// ---------------------------------------------------------------------------
// DIO action callbacks
// ---------------------------------------------------------------------------

void LM_NexusModule::setDioActionForReceiving(void (*action)()) {
    dio_rx_action_ = action;
}

void LM_NexusModule::setDioActionForReceivingTimeout(void (*)()){
    // Nexus has no hardware timeout; handled by polling loop
}

void LM_NexusModule::setDioActionForScanning(void (*)()){
}

void LM_NexusModule::setDioActionForScanningTimeout(void (*)()){
}

void LM_NexusModule::clearDioActions() {
    dio_rx_action_ = nullptr;
}

// ---------------------------------------------------------------------------
// Radio parameter setters (stored for time-on-air; no hardware to configure)
// ---------------------------------------------------------------------------

int16_t LM_NexusModule::setFrequency(float freq) {
    freq_ = freq;
    return RADIOLIB_ERR_NONE;
}

int16_t LM_NexusModule::setBandwidth(float bw) {
    bw_ = bw;
    return RADIOLIB_ERR_NONE;
}

int16_t LM_NexusModule::setSpreadingFactor(uint8_t sf) {
    sf_ = sf;
    return RADIOLIB_ERR_NONE;
}

int16_t LM_NexusModule::setCodingRate(uint8_t cr) {
    cr_ = cr;
    return RADIOLIB_ERR_NONE;
}

int16_t LM_NexusModule::setSyncWord(uint8_t) { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::setOutputPower(int8_t) { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::setPreambleLength(int16_t) { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::setGain(uint8_t) { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::setOutputPower(int8_t, int8_t) { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::setCRC(bool) { return RADIOLIB_ERR_NONE; }

// ---------------------------------------------------------------------------
// Misc stubs
// ---------------------------------------------------------------------------

float LM_NexusModule::getRSSI() { return float_from_file(rssi_path_); }
float LM_NexusModule::getSNR() { return float_from_file(snr_path_); }
int16_t LM_NexusModule::scanChannel() { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::startChannelScan() { return RADIOLIB_ERR_NONE; }
int16_t LM_NexusModule::standby() { return RADIOLIB_ERR_NONE; }
void LM_NexusModule::reset() {}
