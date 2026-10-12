# Technical details

PVeinMiner is a PumpkinMC plugin written in C++. It listens for block-break events and mines the rest of a connected ore vein for the player.

## How the code is split up

- `src/main.cpp` sets up the plugin, registers the reload command, and listens for block-break events.
- `src/config.cpp` reads the config file, keeps the block list, and calculates experience drops.
- `src/vein_search.cpp` searches around the block the player broke and returns the connected blocks.
- `src/veinminer.cpp` reads the held tool, spawns drops and experience, removes the extra blocks, and plays the break effects.

## How vein search works

The search starts at the block from the event and checks all 26 positions around each ore it finds. A neighbor is added if it is in the configured block list and belongs to the same ore family.

Minecraft's `minecraft:` namespace is ignored for family matching. The `deepslate_` prefix is ignored too, so regular and deepslate versions of the same ore can join one vein.

Face-adjacent blocks are added directly. Diagonal blocks are added when at least one of the blocks between them is air. This lets exposed diagonal ores count without connecting ores hidden behind solid blocks.

The positions are stored on the heap for the duration of the search. `max_vein_blocks` defaults to 128 and has no fixed plugin limit. Set it to a positive whole number. Very large values can use a lot of memory, and the search stops if the requested allocation is too large or fails.

## What happens when a block is broken

The event handler returns early if vein mining is disabled, the event has no player, or the block is not in the configured list. It then searches for the connected blocks.

The block that triggered the event is left to normal Minecraft mining. For each other block, the plugin:

1. Reads the block ID and creates a loot command using the player's held tool.
2. Includes Fortune and Silk Touch levels in that command when the tool has them.
3. Calculates configured experience for the block and spawns an experience orb when the result is above zero.
4. Replaces the block with air while skipping its normal drops, since the loot command already handled them.
5. Plays the block-break particles and sound.

The loot and experience commands run as the console in the block's dimension. Experience is based on the configured min and max values, the world age, and the block position. This makes the result repeatable for a given world age and position. Silk Touch blocks do not give extra experience.

## Configuration

The plugin creates `config.yml` in its data folder the first time it runs. The default vein size is 128. Set it to any positive whole number that fits the platform's `size_t` type:

```yaml
enabled: true
debug: false
max_vein_blocks: 128

blocks:
  "minecraft:diamond_ore":
    min: 3
    max: 7
  "minecraft:deepslate_diamond_ore":
    min: 3
    max: 7
```

Only blocks in the `blocks` list can start or join a vein. A block entry has a minimum and maximum experience value. Both must be present, and the minimum must not be greater than the maximum.

The block table can hold up to 64 entries. Adding a custom `blocks` list replaces the default list, so keep the default entries in the file if you still want those ores enabled. Block IDs can be written with or without the `minecraft:` namespace.

Experience values are limited to 32767. The config reader supports the settings shown above and the older `ore=block_id,min,max` format. It reads a small YAML-like format; it is not a general YAML parser.

## Reload command

Use `/pveinminer reload` to read the config file again. The command requires the `pveinminer:reload` permission, which defaults to operator permission level two.

## Build

The build script expects the Pumpkin API checkout to contain `include/pumpkin_api.h` and `lib/libpumpkin-api.a`. By default it looks for a `pumpkin-api` folder inside the project. To use another checkout, set `PUMPKIN_API_DIR`.

From the project folder, run:

```sh
sh src/build.sh
```

This builds `src/pveinminer.wasm` with `wasm32-wasip2-clang++` as a WASI reactor plugin.
