#include "config.hpp"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace veinminer {

// Shared configuration used by the plugin entry point and mining logic.
Config config;

static char *trim(char *text);

namespace {

struct DefaultBlock {
    const char *block_id;
    uint32_t experience_min;
    uint32_t experience_max;
};

static constexpr DefaultBlock default_blocks[] = {
    // Default ores and experience ranges used when no custom block list is configured.
    {"minecraft:ancient_debris", 0, 0},
    {"minecraft:diamond_ore", 3, 7},
    {"minecraft:deepslate_diamond_ore", 3, 7},
    {"minecraft:emerald_ore", 3, 7},
    {"minecraft:deepslate_emerald_ore", 3, 7},
    {"minecraft:lapis_ore", 2, 5},
    {"minecraft:deepslate_lapis_ore", 2, 5},
    {"minecraft:iron_ore", 0, 0},
    {"minecraft:deepslate_iron_ore", 0, 0},
    {"minecraft:gold_ore", 0, 0},
    {"minecraft:deepslate_gold_ore", 0, 0},
    {"minecraft:copper_ore", 0, 0},
    {"minecraft:deepslate_copper_ore", 0, 0},
    {"minecraft:nether_gold_ore", 0, 1},
    {"minecraft:nether_quartz_ore", 2, 5},
    {"minecraft:redstone_ore", 1, 5},
    {"minecraft:deepslate_redstone_ore", 1, 5},
    {"minecraft:coal_ore", 0, 2},
    {"minecraft:deepslate_coal_ore", 0, 2},
};

static bool parse_unsigned(char *text, uint32_t &value) {
    // Parse a non-negative integer and reject values outside the supported range.
    text = trim(text);
    if (*text == '\0') {
        return false;
    }

    char *end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (*end != '\0' || parsed > 32767) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

static bool parse_vein_limit(char *text, size_t &value) {
    text = trim(text);
    if (*text == '\0' || *text == '-') {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed == 0 || parsed > SIZE_MAX) {
        return false;
    }
    value = static_cast<size_t>(parsed);
    return true;
}

static void strip_minecraft_prefix(std::string_view &name) {
    constexpr std::string_view prefix = "minecraft:";
    if (name.size() < prefix.size()) {
        return;
    }
    for (size_t index = 0; index < prefix.size(); ++index) {
        if (name[index] != prefix[index]) {
            return;
        }
    }
    name = std::string_view(name.data() + prefix.size(), name.size() - prefix.size());
}

static bool same_block_id(std::string_view first, std::string_view second) {
    strip_minecraft_prefix(first);
    strip_minecraft_prefix(second);
    return first == second;
}

static bool parse_legacy_ore_entry(char *line, BlockConfig &block) {
    // Parse the legacy ore=id,min,max format for existing config files.
    char *block_id = trim(line + 4);
    char *first_separator = strchr(block_id, ',');
    if (!first_separator) {
        return false;
    }
    *first_separator = '\0';
    char *minimum_text = first_separator + 1;
    char *second_separator = strchr(minimum_text, ',');
    if (!second_separator) {
        return false;
    }
    *second_separator = '\0';

    block_id = trim(block_id);
    if (*block_id == '\0' || strlen(block_id) >= sizeof(block.block_id) ||
        !parse_unsigned(minimum_text, block.experience_min) ||
        !parse_unsigned(second_separator + 1, block.experience_max) ||
        block.experience_min > block.experience_max) {
        return false;
    }
    strcpy(block.block_id, block_id);
    return true;
}

static bool parse_block_key(char *entry, BlockConfig &block) {
    // Parse a quoted block ID used as a key in the blocks section.
    if (*entry != '"') {
        return false;
    }
    char *closing_quote = strchr(entry + 1, '"');
    if (!closing_quote) {
        return false;
    }
    *closing_quote = '\0';
    if (strcmp(trim(closing_quote + 1), ":") != 0) {
        return false;
    }

    const char *block_id = entry + 1;
    const size_t block_id_length = strlen(block_id);
    if (block_id_length == 0 || block_id_length >= sizeof(block.block_id)) {
        return false;
    }
    memcpy(block.block_id, block_id, block_id_length + 1);
    return true;
}

}

static char *trim(char *text) {
    // Remove spaces, tabs, and line endings from both ends of the string.
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        ++text;
    }

    char *end = text + strlen(text);
    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return text;
}

static bool write_default_config(const char *path) {
    // Create a config file with the default settings and ore list.
    FILE *file = fopen(path, "w");
    if (!file) {
        printf("[VeinMiner] Could not create config file at %s\n", path);
        return false;
    }

    fputs("# Set enabled to false to disable vein mining.\n"
          "enabled: true\n"
          "debug: false\n"
          "max_vein_blocks: 128\n"
          "\n"
          "blocks:\n", file);
    for (const auto &block : default_blocks) {
        fprintf(file, "  \"%s\":\n    min: %u\n    max: %u\n",
                block.block_id,
                static_cast<unsigned int>(block.experience_min),
                static_cast<unsigned int>(block.experience_max));
    }
    fclose(file);
    return true;
}

static void write_config(const char *path, bool enabled, bool debug,
                         size_t max_vein_blocks, const BlockConfig *blocks,
                         size_t block_count) {
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "[VeinMiner] Could not update config file at %s\n", path);
        return;
    }

    // Write the global settings first, followed by each configured block.
    fprintf(file, "enabled: %s\ndebug: %s\nmax_vein_blocks: %zu\n\n",
            enabled ? "true" : "false", debug ? "true" : "false",
            max_vein_blocks);
    fputs("blocks:\n", file);
    for (size_t index = 0; index < block_count; ++index) {
        fprintf(file, "  \"%s\":\n    min: %u\n    max: %u\n",
                blocks[index].block_id,
                static_cast<unsigned int>(blocks[index].experience_min),
                static_cast<unsigned int>(blocks[index].experience_max));
    }
    fclose(file);
}

void Config::add_block(std::string_view block_id, uint32_t experience_min,
                       uint32_t experience_max) {
    // Update an existing block entry, or add it if there is room in the table.
    if (block_id.empty() || block_id.size() >= sizeof(blocks_[0].block_id)) {
        return;
    }
    for (size_t index = 0; index < block_count_; ++index) {
        if (same_block_id(blocks_[index].block_id, block_id)) {
            strncpy(blocks_[index].block_id, block_id.data(),
                    sizeof(blocks_[index].block_id) - 1);
            blocks_[index].block_id[sizeof(blocks_[index].block_id) - 1] = '\0';
            blocks_[index].experience_min = experience_min;
            blocks_[index].experience_max = experience_max;
            return;
        }
    }
    if (block_count_ == blocks_.size()) {
        return;
    }

    auto &block = blocks_[block_count_++];
    memcpy(block.block_id, block_id.data(), block_id.size());
    block.block_id[block_id.size()] = '\0';
    block.experience_min = experience_min;
    block.experience_max = experience_max;
}

void Config::load(pumpkin_plugin_context_borrow_context_t context) {
    // Get the plugin data folder and build the config file path.
    plugin_string_t data_folder{};
    pumpkin_plugin_context_method_context_get_data_folder(context, &data_folder);
    if (!data_folder.ptr || data_folder.len == 0 || data_folder.len > 400) {
        config_path_[0] = '\0';
        free(data_folder.ptr);
        reload();
        return;
    }

    const int path_length = snprintf(
        config_path_, sizeof(config_path_), "%.*s/config.yml",
        static_cast<int>(data_folder.len),
        reinterpret_cast<const char *>(data_folder.ptr));
    free(data_folder.ptr);
    if (path_length <= 0 || static_cast<size_t>(path_length) >= sizeof(config_path_)) {
        config_path_[0] = '\0';
    }
    reload();
}

bool Config::reload() {
    // Reset to defaults before applying values from the config file.
    enabled_ = true;
    debug_ = false;
    max_vein_blocks_ = 128;
    block_count_ = 0;
    for (const auto &block : default_blocks) {
        add_block(block.block_id, block.experience_min, block.experience_max);
    }

    if (config_path_[0] == '\0') {
        fprintf(stderr, "[VeinMiner] Config path is unavailable; using defaults\n");
        return false;
    }

    FILE *file = fopen(config_path_, "r");
    if (!file) {
        return write_default_config(config_path_);
    }

    // Track whether custom or legacy block entries replace the default list.
    bool saw_custom_blocks = false;
    bool saw_legacy_ores = false;
    bool in_blocks = false;
    bool pending_block = false;
    bool pending_min = false;
    bool pending_max = false;
    BlockConfig current_block{};
    // Add a parsed block after its min and max experience values have been read.
    const auto commit_block = [&]() {
        if (!pending_block) {
            return;
        }
        if (pending_min && pending_max &&
            current_block.experience_min <= current_block.experience_max) {
            add_block(current_block.block_id, current_block.experience_min,
                      current_block.experience_max);
        } else {
            fprintf(stderr, "[VeinMiner] Ignoring block config without valid min/max: %s\n",
                    current_block.block_id);
        }
        pending_block = false;
    };

    char line[256];
    // Read the config one line at a time using its supported YAML-like format.
    while (fgets(line, sizeof(line), file)) {
        size_t indentation = 0;
        while (line[indentation] == ' ' || line[indentation] == '\t') {
            ++indentation;
        }
        char *entry = trim(line);
        if (*entry == '\0' || *entry == '#') {
            continue;
        }

        // Parse top-level settings and detect when the blocks section begins.
        if (indentation == 0) {
            commit_block();
            if (strcmp(entry, "blocks:") == 0) {
                in_blocks = true;
                continue;
            }
            in_blocks = false;

            if (strncmp(entry, "ore=", 4) == 0) {
                BlockConfig block{};
                if (!parse_legacy_ore_entry(entry, block)) {
                    fprintf(stderr, "[VeinMiner] Ignoring invalid legacy ore entry\n");
                    continue;
                }
                if (!saw_custom_blocks) {
                    block_count_ = 0;
                    saw_custom_blocks = true;
                }
                saw_legacy_ores = true;
                add_block(block.block_id, block.experience_min, block.experience_max);
                continue;
            }

            char *separator = strchr(entry, ':');
            if (!separator) {
                continue;
            }
            *separator = '\0';
            char *key = trim(entry);
            char *value = trim(separator + 1);

            if (strcmp(key, "enabled") == 0 || strcmp(key, "debug") == 0) {
                bool parsed_value;
                if (strcmp(value, "true") == 0) {
                    parsed_value = true;
                } else if (strcmp(value, "false") == 0) {
                    parsed_value = false;
                } else {
                    fprintf(stderr, "[VeinMiner] Invalid boolean config value for %s\n", key);
                    continue;
                }
                if (strcmp(key, "enabled") == 0) {
                    enabled_ = parsed_value;
                } else {
                    debug_ = parsed_value;
                }
            } else if (strcmp(key, "max_vein_blocks") == 0) {
                size_t parsed_value;
                if (parse_vein_limit(value, parsed_value)) {
                    max_vein_blocks_ = parsed_value;
                } else {
                    fprintf(stderr,
                            "[VeinMiner] Invalid max_vein_blocks config value; using previous value\n");
                }
            }
            continue;
        }

        if (!in_blocks) {
            continue;
        }
        // Each quoted key starts a new block entry, so finish the previous one first.
        if (indentation == 2) {
            commit_block();
            BlockConfig next_block{};
            if (!parse_block_key(entry, next_block)) {
                fprintf(stderr, "[VeinMiner] Ignoring invalid blocks mapping key\n");
                continue;
            }
            if (!saw_custom_blocks) {
                block_count_ = 0;
                saw_custom_blocks = true;
            }
            current_block = next_block;
            pending_block = true;
            pending_min = false;
            pending_max = false;
            continue;
        }
        if (indentation >= 4 && pending_block) {
            char *separator = strchr(entry, ':');
            if (!separator) {
                continue;
            }
            *separator = '\0';
            char *key = trim(entry);
            char *value = trim(separator + 1);
            if (strcmp(key, "min") == 0) {
                pending_min = parse_unsigned(value, current_block.experience_min);
            } else if (strcmp(key, "max") == 0) {
                pending_max = parse_unsigned(value, current_block.experience_max);
            }
        }
    }
    commit_block();
    fclose(file);
    if (!saw_custom_blocks || saw_legacy_ores) {
        write_config(config_path_, enabled_, debug_, max_vein_blocks_,
                     blocks_.data(), block_count_);
    }
    if (debug_) {
        printf("[VeinMiner] Config enabled: %s, block entries: %u, max vein: %zu\n",
               enabled_ ? "true" : "false",
               static_cast<unsigned int>(block_count_),
               max_vein_blocks_);
    }
    return true;
}

bool Config::enabled() const {
    return enabled_;
}

bool Config::debug() const {
    return debug_;
}

size_t Config::max_vein_blocks() const {
    return max_vein_blocks_;
}

bool Config::is_block(std::string_view block_id) const {
    // Check whether the block ID matches any configured entry.
    for (size_t index = 0; index < block_count_; ++index) {
        if (same_block_id(blocks_[index].block_id, block_id)) {
            return true;
        }
    }
    return false;
}

uint32_t Config::experience_for_block(std::string_view block_id,
                                      pumpkin_plugin_world_block_pos_t position,
                                      uint64_t world_age,
                                      bool silk_touch) const {
    // Silk Touch suppresses the experience drop.
    if (silk_touch) {
        return 0;
    }

    for (size_t index = 0; index < block_count_; ++index) {
        const auto &block = blocks_[index];
        if (!same_block_id(block.block_id, block_id)) {
            continue;
        }

        // Derive a repeatable value from the world age and block position.
        uint64_t seed = world_age ^
            (static_cast<uint64_t>(static_cast<uint32_t>(position.x)) << 32) ^
            (static_cast<uint64_t>(static_cast<uint32_t>(position.y)) << 16) ^
            static_cast<uint32_t>(position.z);
        seed += 0x9e3779b97f4a7c15ULL;
        seed = (seed ^ (seed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        seed = (seed ^ (seed >> 27)) * 0x94d049bb133111ebULL;
        seed ^= seed >> 31;
        return block.experience_min + static_cast<uint32_t>(
            seed % (block.experience_max - block.experience_min + 1));
    }
    return 0;
}

}
