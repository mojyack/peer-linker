#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

struct ServerArgs {
    bool    help                   = false;
    bool    verbose                = false;
    bool    websocket_verbose      = false;
    bool    websocket_dump_packets = false;
    uint8_t libws_debug_bitmap     = 0b11; // LLL_ERR | LLL_WARN

    static auto parse(const int argc, const char* argv[], std::string_view program_name) -> std::optional<ServerArgs>;
};
