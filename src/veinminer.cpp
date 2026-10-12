#include "veinminer.hpp"
#include "config.hpp"
#include "vein_search.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace veinminer {
namespace {

// Item data needed to generate drops for the held tool.
struct MiningTool {
    char item_argument[256]{};
    bool silk_touch = false;
};

void log(const char *message) {
    // Print plugin messages only when debug mode is enabled.
    if (config.debug()) {
        printf("[VeinMiner] %s\n", message);
    }
}

void log_vein_size(size_t block_count) {
    if (config.debug()) {
        printf("[VeinMiner] Found %zu block(s) in the connected ore vein\n",
               block_count);
    }
}

bool read_mining_tool(int32_t player_handle, MiningTool &tool) {
    // Read the held item and its enchantments from the player's right hand.
    pumpkin_plugin_player_own_item_stack_t held_item{};
    if (!pumpkin_plugin_player_method_player_get_item_in_hand(
            {player_handle}, PUMPKIN_PLUGIN_COMMON_HAND_RIGHT, &held_item)) {
        return false;
    }

    const auto item = pumpkin_plugin_item_stack_borrow_item_stack(held_item);
    plugin_string_t registry_key{};
    pumpkin_plugin_item_stack_method_item_stack_get_registry_key(item, &registry_key);
    pumpkin_plugin_item_stack_list_enchantment_value_t enchantments{};
    pumpkin_plugin_item_stack_method_item_stack_get_enchantments(item, &enchantments);

    // Store the Fortune and Silk Touch levels used by loot and XP calculations.
    uint32_t fortune = 0;
    uint32_t silk_touch = 0;
    for (size_t index = 0; index < enchantments.len; ++index) {
        const auto &enchantment = enchantments.ptr[index];
        if (enchantment.enchantment == PUMPKIN_PLUGIN_ENCHANTMENTS_ENCHANTMENT_FORTUNE) {
            fortune = enchantment.level;
        } else if (enchantment.enchantment ==
                   PUMPKIN_PLUGIN_ENCHANTMENTS_ENCHANTMENT_SILK_TOUCH) {
            silk_touch = enchantment.level;
        }
    }
    pumpkin_plugin_item_stack_list_enchantment_value_free(&enchantments);

    // Include supported enchantment levels in the item argument for the loot command.
    const char *item_id = reinterpret_cast<const char *>(registry_key.ptr);
    int length = -1;
    if (item_id && registry_key.len > 0 && registry_key.len < 96) {
        const char *format = "%.*s";
        if (fortune && silk_touch) {
            format = "%.*s[minecraft:enchantments={levels:{\"minecraft:fortune\":%u,\"minecraft:silk_touch\":%u}}]";
            length = snprintf(
                tool.item_argument, sizeof(tool.item_argument), format,
                static_cast<int>(registry_key.len), item_id, fortune, silk_touch);
        } else if (fortune) {
            format = "%.*s[minecraft:enchantments={levels:{\"minecraft:fortune\":%u}}]";
            length = snprintf(
                tool.item_argument, sizeof(tool.item_argument), format,
                static_cast<int>(registry_key.len), item_id, fortune);
        } else if (silk_touch) {
            format = "%.*s[minecraft:enchantments={levels:{\"minecraft:silk_touch\":%u}}]";
            length = snprintf(
                tool.item_argument, sizeof(tool.item_argument), format,
                static_cast<int>(registry_key.len), item_id, silk_touch);
        } else {
            length = snprintf(
                tool.item_argument, sizeof(tool.item_argument), format,
                static_cast<int>(registry_key.len), item_id);
        }
    }

    free(registry_key.ptr);
    pumpkin_plugin_item_stack_item_stack_drop_own(held_item);
    tool.silk_touch = silk_touch > 0;
    return length > 0 && static_cast<size_t>(length) < sizeof(tool.item_argument);
}

void execute_command(pumpkin_plugin_server_borrow_server_t server,
                     pumpkin_plugin_server_command_sender_t &sender,
                     char *command, int length, size_t capacity) {
    // Do not execute a command if formatting failed or exceeded the buffer.
    if (length <= 0 || static_cast<size_t>(length) >= capacity) {
        return;
    }
    plugin_string_t text{
        reinterpret_cast<uint8_t *>(command), static_cast<size_t>(length)};
    pumpkin_plugin_server_method_server_execute_command(server, &text, &sender);
}

std::string_view block_name(const pumpkin_plugin_world_block_t &block) {
    const char *name = reinterpret_cast<const char *>(block.name.ptr);
    return {name ? name : "", block.name.len};
}

void spawn_break_effects(int32_t player_handle,
                         pumpkin_plugin_world_block_pos_t position) {
    // Spawn the block-break particles and sound at the block position.
    pumpkin_plugin_player_position_t effect_position{
        static_cast<double>(position.x) + 0.5,
        static_cast<double>(position.y) + 0.5,
        static_cast<double>(position.z) + 0.5,
    };
    pumpkin_plugin_player_position_t particle_offset{0.25, 0.25, 0.25};
    pumpkin_plugin_player_method_player_spawn_particles(
        {player_handle}, PUMPKIN_PLUGIN_PARTICLES_PARTICLE_POOF,
        &effect_position, 6, &particle_offset, 0.025f);

    static uint8_t sound_bytes[] = "minecraft:block.stone.break";
    plugin_string_t sound{sound_bytes, sizeof(sound_bytes) - 1};
    pumpkin_plugin_player_method_player_play_custom_sound_at(
        {player_handle}, &effect_position, &sound,
        PUMPKIN_PLUGIN_SOUNDS_SOUND_CATEGORY_BLOCKS, 0.4f, 1.1f);
}

void mine_block(pumpkin_plugin_world_borrow_world_t world,
                pumpkin_plugin_server_borrow_server_t server,
                pumpkin_plugin_server_command_sender_t &sender,
                int32_t player_handle,
                pumpkin_plugin_world_block_pos_t position,
                const char *dimension,
                const MiningTool &tool,
                uint64_t world_age) {
    // Read the block ID before replacing the block with air.
    pumpkin_plugin_world_block_t block{};
    pumpkin_plugin_world_method_world_get_block(world, &position, &block);
    const auto mined_name = block_name(block);

    char loot_command[512];
    const int loot_length = snprintf(
        loot_command, sizeof(loot_command),
        "execute in %s run loot spawn %.1f %.1f %.1f mine %d %d %d %s",
        dimension, static_cast<double>(position.x) + 0.5,
        static_cast<double>(position.y) + 0.5,
        static_cast<double>(position.z) + 0.5,
        position.x, position.y, position.z, tool.item_argument);
    execute_command(server, sender, loot_command, loot_length, sizeof(loot_command));

    // Calculate and spawn configured experience separately from item drops.
    const uint32_t experience = config.experience_for_block(
        mined_name, position, world_age, tool.silk_touch);
    if (experience > 0) {
        char command[256];
        const int length = snprintf(
            command, sizeof(command),
            "execute in %s run summon experience_orb %.1f %.1f %.1f {Value:%us}",
            dimension, static_cast<double>(position.x) + 0.5,
            static_cast<double>(position.y) + 0.5,
            static_cast<double>(position.z) + 0.5,
            static_cast<unsigned int>(experience));
        execute_command(server, sender, command, length, sizeof(command));
    }
    pumpkin_plugin_world_block_free(&block);

    // Replace the block with air and skip drops because the loot command handled them.
    static uint8_t air_bytes[] = "minecraft:air";
    plugin_string_t air{air_bytes, sizeof(air_bytes) - 1};
    const uint8_t flags = PUMPKIN_PLUGIN_WORLD_BLOCK_FLAGS_NOTIFY_NEIGHBORS |
                          PUMPKIN_PLUGIN_WORLD_BLOCK_FLAGS_NOTIFY_LISTENERS |
                          PUMPKIN_PLUGIN_WORLD_BLOCK_FLAGS_SKIP_DROPS;
    pumpkin_plugin_world_method_world_set_block_by_name(world, &position, &air, flags);
    spawn_break_effects(player_handle, position);
}

bool prepare_mining(pumpkin_plugin_world_borrow_world_t world,
                    int32_t player_handle,
                    MiningTool &tool,
                    char (&dimension_name)[128], uint64_t &world_age) {
    // Read values shared by every block before starting the mining loop.
    plugin_string_t dimension{};
    pumpkin_plugin_world_method_world_get_dimension(world, &dimension);
    const bool valid_dimension =
        dimension.ptr && dimension.len > 0 && dimension.len < 128;
    const bool valid_tool = read_mining_tool(player_handle, tool);
    if (!valid_dimension || !valid_tool) {
        free(dimension.ptr);
        return false;
    }
    memcpy(dimension_name, dimension.ptr, dimension.len);
    dimension_name[dimension.len] = '\0';
    free(dimension.ptr);
    world_age = pumpkin_plugin_world_method_world_get_world_age(world);
    return true;
}

void mine_remaining_vein(plugin_own_server_instance_t server,
                         pumpkin_plugin_world_borrow_world_t world,
                         int32_t player_handle,
                         const Vein &vein) {
    // Prepare the tool, dimension, and world age once for the entire vein.
    MiningTool tool{};
    char dimension[128];
    uint64_t world_age = 0;
    if (!prepare_mining(world, player_handle, tool, dimension, world_age)) {
        log("Could not read held tool or world dimension; leaving vein blocks intact");
        return;
    }

    auto borrowed_server = pumpkin_plugin_server_borrow_server(server);
    pumpkin_plugin_server_command_sender_t sender{};
    sender.tag = PUMPKIN_PLUGIN_SERVER_COMMAND_SENDER_CONSOLE;
    // Skip index zero because it is the block that triggered the event.
    for (size_t index = 1; index < vein.size; ++index) {
        mine_block(world, borrowed_server, sender, player_handle,
                   vein.positions[index], dimension, tool, world_age);
    }
    pumpkin_plugin_server_server_drop_borrow(borrowed_server);
}

}

void on_block_break(plugin_own_server_instance_t server, plugin_event_t *event) {
    // Validate the event, find the connected vein, and mine its remaining blocks.
    if (!config.enabled() || !event ||
        event->tag != PUMPKIN_PLUGIN_EVENT_EVENT_TYPE_BLOCK_BREAK_EVENT) {
        return;
    }

    auto &data = event->val.block_break_event;
    if (!data.player.is_some) {
        return;
    }

    const char *name = reinterpret_cast<const char *>(data.block.ptr);
    const std::string_view mined_block(name ? name : "", data.block.len);
    if (!config.is_block(mined_block)) {
        return;
    }

    auto world = pumpkin_plugin_player_method_player_get_world(
        {data.player.val.__handle});
    pumpkin_plugin_world_borrow_world_t borrowed_world{world.__handle};
    const pumpkin_plugin_world_block_pos_t start{
        data.block_pos.x, data.block_pos.y, data.block_pos.z};
    Vein vein{};
    const bool found_vein = find_vein(
        borrowed_world, start, mined_block, config.max_vein_blocks(), vein);
    if (!found_vein) {
        log("Could not allocate vein search storage");
    } else {
        log_vein_size(vein.size);
    }
    if (found_vein && vein.size > 1) {
        mine_remaining_vein(server, borrowed_world,
                            data.player.val.__handle, vein);
    }
    free(vein.positions);
    pumpkin_plugin_world_world_drop_own(world);
}
}
