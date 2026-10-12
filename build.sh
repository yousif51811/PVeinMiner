#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Use the default API checkout unless a different path is provided.
API_DIR=${PUMPKIN_API_DIR:-"$SCRIPT_DIR/../pumpkin-api"}

if [ ! -r "$API_DIR/lib/libpumpkin-api.a" ]; then
    printf 'Cannot read API archive: %s/lib/libpumpkin-api.a\n' "$API_DIR" >&2
    exit 1
fi

cd "$SCRIPT_DIR"
# Compile the plugin entry point, config, vein search, and mining event handler.
wasm32-wasip2-clang++ -Oz \
    -I"$API_DIR/include" \
    "$API_DIR/lib/libpumpkin-api.a" \
    main.cpp \
    config.cpp \
    vein_search.cpp \
    veinminer.cpp \
    -o pveinminer.wasm \
    -mexec-model=reactor
