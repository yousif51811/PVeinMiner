#include "pumpkin_api.h"
#include "config.hpp"
#include "veinminer.hpp"
#include <stdio.h>
#include <stdlib.h>

// Handle the /pveinminer reload command and send the result to its sender.
static bool reload_config_command(plugin_own_command_sender_t sender,
                                  plugin_own_server_instance_t server,
                                  plugin_own_consumed_args_t args,
                                  int32_t *ret,
                                  plugin_command_error_t *err) {
    (void)err;
    const bool reloaded = veinminer::config.reload();
    static uint8_t success_bytes[] = "PVeinMiner config reloaded.";
    static uint8_t failure_bytes[] = "PVeinMiner config reload failed.";
    plugin_string_t feedback = reloaded
        ? plugin_string_t{success_bytes, sizeof(success_bytes) - 1}
        : plugin_string_t{failure_bytes, sizeof(failure_bytes) - 1};
    const auto text = pumpkin_plugin_text_static_text_component_text(&feedback);
    const auto borrowed_sender = pumpkin_plugin_command_borrow_command_sender(sender);
    // Report whether the config was reloaded successfully.
    if (reloaded) {
        pumpkin_plugin_command_method_command_sender_send_system_message(
            borrowed_sender, text);
        *ret = 1;
    } else {
        pumpkin_plugin_command_method_command_sender_send_error(
            borrowed_sender, text);
        *ret = 0;
    }

    pumpkin_plugin_command_command_sender_drop_own(sender);
    pumpkin_plugin_command_consumed_args_drop_own(args);
    pumpkin_plugin_server_server_drop_own(server);
    return true;
}

static void register_reload_command(pumpkin_plugin_context_borrow_context_t context) {
    // Register the permission required to use the reload command.
    static uint8_t permission_name_bytes[] = "pveinminer:reload";
    static uint8_t permission_description_bytes[] = "Reload PVeinMiner configuration";
    plugin_string_t permission_name{
        permission_name_bytes, sizeof(permission_name_bytes) - 1};
    plugin_string_t permission_description{
        permission_description_bytes, sizeof(permission_description_bytes) - 1};
    pumpkin_plugin_permission_permission_t permission{};
    permission.node = permission_name;
    permission.description = permission_description;
    permission.default_.tag = PUMPKIN_PLUGIN_PERMISSION_PERMISSION_DEFAULT_OP;
    permission.default_.val.op = PUMPKIN_PLUGIN_PERMISSION_PERMISSION_LEVEL_TWO;

    plugin_string_t permission_error{};
    if (!pumpkin_plugin_context_method_context_register_permission(
            context, &permission, &permission_error)) {
        fprintf(stderr, "[PVeinMiner] Could not register reload permission\n");
        free(permission_error.ptr);
        return;
    }
    free(permission_error.ptr);

    static uint8_t root_name_bytes[] = "pveinminer";
    static uint8_t description_bytes[] = "PVeinMiner commands";
    static uint8_t reload_name_bytes[] = "reload";
    plugin_string_t root_name{root_name_bytes, sizeof(root_name_bytes) - 1};
    plugin_string_t description{description_bytes, sizeof(description_bytes) - 1};
    plugin_string_t reload_name{reload_name_bytes, sizeof(reload_name_bytes) - 1};
    plugin_string_t command_names_storage[] = {root_name};
    plugin_list_string_t command_names{command_names_storage, 1};

    // Create the root command and add its reload subcommand.
    const auto command =
        pumpkin_plugin_command_constructor_command(&command_names, &description);
    const auto reload_node =
        pumpkin_plugin_command_static_command_node_literal(&reload_name);
    pumpkin_command_node_execute_with_handler(
        pumpkin_plugin_command_borrow_command_node(reload_node),
        reload_config_command);
    pumpkin_plugin_command_method_command_then(
        pumpkin_plugin_command_borrow_command(command), reload_node);
    pumpkin_plugin_context_method_context_register_command(
        context, command, &permission_name);
}

pumpkin_metadata_t get_meta(void) {
    // Metadata shown by the plugin host.
    static const char* authors[] = {"Zerorifter"};
    static const char* permissions[] = {"fs.read.data", "fs.write.data"};
    return (pumpkin_metadata_t) {
        .name = "pveinminer",
        .version = "0.1.1",
        .authors = authors,
        .authors_count = 1,
        .description = "Mine the whole vein on mining a single ore to make mining more fun.",
        .dependencies_count = 0,
        .permissions = permissions,
        .permissions_count = 2
    };
}

void on_load(plugin_own_context_t ctx) {
    // Load the config, register the command, then subscribe to block-break events.
    auto borrowed_context = pumpkin_plugin_context_borrow_context(ctx);
    veinminer::config.load(borrowed_context);
    if (veinminer::config.debug()) {
        printf("[PVeinMiner] loaded!\n");
    }
    register_reload_command(borrowed_context);
    pumpkin_context_register_event(
        borrowed_context,
        veinminer::on_block_break,
        PUMPKIN_PLUGIN_EVENT_EVENT_TYPE_BLOCK_BREAK_EVENT,
        PUMPKIN_PLUGIN_EVENT_EVENT_PRIORITY_NORMAL,
        false
    );
}

REGISTER_PUMPKIN_PLUGIN(((pumpkin_plugin_t){
    .get_metadata = get_meta,
    .on_load = on_load
}))
