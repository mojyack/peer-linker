#include <string_view>

#include "macros/unwrap.hpp"
#include "server-args.hpp"
#include "util/charconv.hpp"

const char* ServerArgs::usage = R"(
    -h          print help
    -v          enable signaling server debug output
    -wv         enable websocket debug output
    -wd         dump every websocket packets
    -wb BITMAP  libwebsockets debug flag bitmap
)";

auto ServerArgs::parse(const int argc, const char* argv[]) -> std::optional<ServerArgs> {
    auto ret = ServerArgs();
    for(auto i = 1; i < argc; i += 1) {
        const auto arg = std::string_view(argv[i]);
        if(arg == "-h") {
            ret.help = true;
        } else if(arg == "-v") {
            ret.verbose = true;
        } else if(arg == "-wv") {
            ret.websocket_verbose = true;
        } else if(arg == "-wd") {
            ret.websocket_dump_packets = true;
        } else if(arg == "-wb") {
            i += 1;
            assert_o(i < argc);
            unwrap_oo(value, from_chars<uint8_t>(argv[i]));
            ret.libws_debug_bitmap = value;
        } else {
            assert_o("unknown argument");
        }
    }
    return ret;
}
