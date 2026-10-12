#ifndef VEINMINER_VEIN_SEARCH_HPP
#define VEINMINER_VEIN_SEARCH_HPP

#include "pumpkin_api.h"
#include <string_view>

namespace veinminer {
// Positions are allocated for each search and stored in discovery order.
struct Vein {
    pumpkin_plugin_world_block_pos_t *positions = nullptr;
    size_t size = 0;
    size_t capacity = 0;
};

// Find touching ore-family blocks, including air-exposed diagonal neighbors.
bool find_vein(pumpkin_plugin_world_borrow_world_t world,
               pumpkin_plugin_world_block_pos_t start,
               std::string_view block_name,
               size_t max_blocks,
               Vein &vein);
}

#endif
