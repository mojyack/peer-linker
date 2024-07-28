#pragma once
#include <cstdint>
#include <optional>

struct ServerArgs {
    static const char* usage;

    bool    help                   = false;
    bool    verbose                = false;
    bool    websocket_verbose      = false;
    bool    websocket_dump_packets = false;
    uint8_t libws_debug_bitmap     = 0b11; // LLL_ERR | LLL_WARN

    static auto parse(const int argc, const char* argv[]) -> std::optional<ServerArgs>;
};
