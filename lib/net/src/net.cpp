#include "net.h"

#include "link_layer.h"

namespace net {

const Address GATEWAY_ADDR = 0;
const Address UNKNOWN_ADDR = 255;

uint32_t MacParams::window_length() {
    return slot_duration() * superframe_slot_count;
}

uint32_t MacParams::slot_duration() {
    return superframe_slot_s + guard_period_s;
}

MacState::MacState(uint32_t start, uint32_t end)
    : window_start(start), window_end(end) {}

MacState::MacState() : window_start(0), window_end(0) {}

LinkState::LinkState()
    : code(link::DEFAULT_CODE),
      tx_bytes_queued(0),
      tx_msg_count(0),
      rx_cursor(nullptr),
      rx_msg_num(0) {}

}  // namespace net
