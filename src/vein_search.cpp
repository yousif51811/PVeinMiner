#include "vein_search.hpp"
#include "config.hpp"
#include <algorithm>
#include <stdint.h>
#include <stdlib.h>

// I hated making this so much

namespace veinminer {
namespace {

// Remove the namespace and deepslate prefixes so matching ores share a family.
std::string_view ore_family_name(std::string_view block_name) {
    constexpr std::string_view namespace_prefix = "minecraft:";
    if (block_name.size() >= namespace_prefix.size()) {
        bool has_namespace = true;
        for (size_t index = 0; index < namespace_prefix.size(); ++index) {
            if (block_name[index] != namespace_prefix[index]) {
                has_namespace = false;
                break;
            }
        }
        if (has_namespace) {
            block_name = std::string_view(
                block_name.data() + namespace_prefix.size(),
                block_name.size() - namespace_prefix.size());
        }
    }

    constexpr std::string_view deepslate_prefix = "deepslate_";
    if (block_name.size() < deepslate_prefix.size()) {
        return block_name;
    }
    for (size_t index = 0; index < deepslate_prefix.size(); ++index) {
        if (block_name[index] != deepslate_prefix[index]) {
            return block_name;
        }
    }
    return std::string_view(block_name.data() + deepslate_prefix.size(),
                            block_name.size() - deepslate_prefix.size());
}

// Check if 2 coords are in the same position
bool same_position(pumpkin_plugin_world_block_pos_t first,
                   pumpkin_plugin_world_block_pos_t second) {
    return first.x == second.x && first.y == second.y && first.z == second.z;
}

// Check if a specific block in a specific dimension is air.
bool is_air_at(pumpkin_plugin_world_borrow_world_t world,
               pumpkin_plugin_world_block_pos_t position) {
    // Release the block value returned by the world API.
    pumpkin_plugin_world_block_t block{};
    pumpkin_plugin_world_method_world_get_block(world, &position, &block);
    const bool result = block.is_air;
    pumpkin_plugin_world_block_free(&block);
    return result;
}
/*
    If a diagonal block is connected to our vein by air, then it counts as part of the vein.
    This is so hidden blocks aren't mined, but diagonal veins still are!
*/ 
bool has_air_bridge(pumpkin_plugin_world_borrow_world_t world,
                    pumpkin_plugin_world_block_pos_t from,
                    int32_t dx, int32_t dy, int32_t dz) {
    if (dx != 0 && is_air_at(world, {from.x + dx, from.y, from.z})) {
        return true;
    }
    if (dy != 0 && is_air_at(world, {from.x, from.y + dy, from.z})) {
        return true;
    }
    if (dz != 0 && is_air_at(world, {from.x, from.y, from.z + dz})) {
        return true;
    }
    return false;
}

// Check if the block is the same ore
// deepslate diamond ore and diamond are still the same family and not completely diffirent veins
bool is_matching_ore(pumpkin_plugin_world_borrow_world_t world,
                     pumpkin_plugin_world_block_pos_t position,
                     std::string_view expected_family) {
    pumpkin_plugin_world_block_t block{};
    pumpkin_plugin_world_method_world_get_block(world, &position, &block);
    const char *name = reinterpret_cast<const char *>(block.name.ptr);
    const std::string_view block_name(name ? name : "", block.name.len);
    const bool matches = name && config.is_block(block_name) &&
        ore_family_name(block_name) == expected_family;
    pumpkin_plugin_world_block_free(&block);
    return matches;
}

bool try_add_neighbor(pumpkin_plugin_world_borrow_world_t world,
                      Vein &vein,
                      pumpkin_plugin_world_block_pos_t origin,
                      int32_t dx, int32_t dy, int32_t dz,
                      std::string_view expected_family) {
    // Count changed axes to distinguish adjacent blocks from diagonal blocks.
    const int changed_axes = (dx != 0) + (dy != 0) + (dz != 0);
    if (changed_axes == 0) {
        return false;
    }
    if (changed_axes > 1 && !has_air_bridge(world, origin, dx, dy, dz)) {
        return false;
    }

    const pumpkin_plugin_world_block_pos_t neighbor{
        origin.x + dx, origin.y + dy, origin.z + dz};
    // Skip positions that have already been added to the vein.
    const auto end = vein.positions + vein.size;
    if (std::any_of(vein.positions, end,
                    [&neighbor](const auto &position) {
                        return same_position(position, neighbor);
                    })) {
        return false;
    }
    if (!is_matching_ore(world, neighbor, expected_family)) {
        return false;
    }

    if (vein.size == vein.capacity) {
        return false;
    }
    vein.positions[vein.size++] = neighbor;
    return true;
}

}

bool find_vein(pumpkin_plugin_world_borrow_world_t world,
               pumpkin_plugin_world_block_pos_t start,
               std::string_view block_name,
               size_t max_blocks,
               Vein &vein) {
    vein.capacity = max_blocks;
    if (vein.capacity == 0 ||
        vein.capacity > SIZE_MAX / sizeof(*vein.positions)) {
        vein.capacity = 0;
        return false;
    }
    vein.positions = static_cast<pumpkin_plugin_world_block_pos_t *>(
        malloc(vein.capacity * sizeof(*vein.positions)));
    if (!vein.positions) {
        vein.capacity = 0;
        return false;
    }

    vein.positions[vein.size++] = start;
    const auto expected_family = ore_family_name(block_name);
    const size_t limit = vein.capacity;

    for (size_t current = 0; current < vein.size && vein.size < limit; ++current) {
        const auto origin = vein.positions[current];
        for (int32_t dx = -1; dx <= 1 && vein.size < limit; ++dx) {
            for (int32_t dy = -1; dy <= 1 && vein.size < limit; ++dy) {
                for (int32_t dz = -1; dz <= 1 && vein.size < limit; ++dz) {
                    try_add_neighbor(world, vein, origin, dx, dy, dz, expected_family);
                }
            }
        }
    }
    return true;
}
}
