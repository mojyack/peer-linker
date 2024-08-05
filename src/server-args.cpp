#include "server-args.hpp"
#include "util/argument-parser.hpp"

auto ServerArgs::parse(const int argc, const char* argv[], std::string_view program_name) -> std::optional<ServerArgs> {
    auto args   = ServerArgs();
    auto parser = args::Parser<uint8_t>();
    parser.kwarg(&args.help, {"-h", "--help"}, {.arg_desc = "print this help message", .state = args::State::Initialized, .no_error_check = true});
    parser.kwarg(&args.verbose, {"-v"}, {.arg_desc = "enable signaling server debug output", .state = args::State::Initialized});
    parser.kwarg(&args.websocket_verbose, {"-wv"}, {.arg_desc = "enable websocket debug output", .state = args::State::Initialized});
    parser.kwarg(&args.websocket_dump_packets, {"-wd"}, {.arg_desc = "dump every websocket packets", .state = args::State::Initialized});
    parser.kwarg(&args.libws_debug_bitmap, {"-wd"}, {"BITMAP", "libwebsockets debug flag bitmap", args::State::DefaultValue});
    if(!parser.parse(argc, argv) || args.help) {
        print("usage: ", program_name, " ", parser.get_help());
        std::exit(0);
    }
    return args;
}
