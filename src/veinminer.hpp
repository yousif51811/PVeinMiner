#ifndef VEINMINER_HPP
#define VEINMINER_HPP

#include "pumpkin_api.h"

namespace veinminer {
// Handle a block-break event and mine the rest of its connected vein.
void on_block_break(plugin_own_server_instance_t server, plugin_event_t *event);
}

#endif
