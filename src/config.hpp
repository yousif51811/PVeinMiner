#ifndef VEINMINER_CONFIG_HPP
#define VEINMINER_CONFIG_HPP

#include "pumpkin_api.h"
#include <array>
#include <string_view>

namespace veinminer {
// Configured block ID and its minimum and maximum experience values.
struct BlockConfig {
    char block_id[96]{};
    uint32_t experience_min = 0;
    uint32_t experience_max = 0;
};

class Config {
public:
    // Maximum number of configured block entries.
    static constexpr size_t max_block_entries = 64;

    // Resolve the config path from the plugin context and load the file.
    void load(pumpkin_plugin_context_borrow_context_t context);
    bool reload();
    bool enabled() const;
    bool debug() const;
    size_t max_vein_blocks() const;
    bool is_block(std::string_view block_id) const;
    uint32_t experience_for_block(std::string_view block_id,
                                  pumpkin_plugin_world_block_pos_t position,
                                  uint64_t world_age,
                                  bool silk_touch) const;

private:
    void add_block(std::string_view block_id, uint32_t experience_min,
                   uint32_t experience_max);

    // Default settings used before values are loaded from a file.
    bool enabled_ = true;
    bool debug_ = false;
    size_t max_vein_blocks_ = 128;
    char config_path_[512]{};
    std::array<BlockConfig, max_block_entries> blocks_{};
    size_t block_count_ = 0;
};

extern Config config;
}

#endif
