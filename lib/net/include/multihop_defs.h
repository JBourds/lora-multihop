#pragma once
#include <stdint.h>

#include "net.h"

namespace net {

extern const Address GATEWAY_ADDR;
extern const Address UNKNOWN_ADDR;

namespace multihop {
// Ring number
typedef uint8_t Ring;

extern const Ring GATEWAY_RING;
extern const Ring UNKNOWN_RING;

}  // namespace multihop
}  // namespace net
