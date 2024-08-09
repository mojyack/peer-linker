#include <cstdint>
#include <optional>
#include <string_view>

#include "macros/unwrap.hpp"
#include "protocol-helper.hpp"
#include "server.hpp"
#include "util/argument-parser.hpp"
#include "ws/misc.hpp"

struct ServerArgs {
    uint16_t port;
    bool     help                   = false;
    bool     verbose                = false;
    bool     websocket_verbose      = false;
    bool     websocket_dump_packets = false;
    uint8_t  libws_debug_bitmap     = 0b11; // LLL_ERR | LLL_WARN

    static auto parse(const int argc, const char* const* argv, std::string_view program_name, uint16_t default_port) -> std::optional<ServerArgs>;
};

auto ServerArgs::parse(const int argc, const char* const* const argv, std::string_view program_name, uint16_t default_port) -> std::optional<ServerArgs> {
    auto args   = ServerArgs{.port = default_port};
    auto parser = args::Parser<uint16_t, uint8_t>();
    parser.kwarg(&args.help, {"-h", "--help"}, {.arg_desc = "print this help message", .state = args::State::Initialized, .no_error_check = true});
    parser.kwarg(&args.port, {"-p"}, {"PORT", "port number to use", args::State::DefaultValue});
    parser.kwarg(&args.verbose, {"-v"}, {.arg_desc = "enable signaling server debug output", .state = args::State::Initialized});
    parser.kwarg(&args.websocket_verbose, {"-wv"}, {.arg_desc = "enable websocket debug output", .state = args::State::Initialized});
    parser.kwarg(&args.websocket_dump_packets, {"-wd"}, {.arg_desc = "dump every websocket packets", .state = args::State::Initialized});
    parser.kwarg(&args.libws_debug_bitmap, {"-wb"}, {"BITMAP", "libwebsockets debug flag bitmap", args::State::DefaultValue});
    if(!parser.parse(argc, argv) || args.help) {
        print("usage: ", program_name, " ", parser.get_help());
        std::exit(0);
    }
    return args;
}

auto run(const int argc, const char* const* const argv,
         const uint16_t                                      default_port,
         Server&                                             server,
         std::unique_ptr<ws::server::SessionDataInitializer> session_initer,
         const char* const                                   protocol,
         const uint16_t                                      error_type) -> bool {
    unwrap_ob(args, ServerArgs::parse(argc, argv, protocol, default_port));
    server.verbose = args.verbose;

    auto& wsctx   = server.websocket_context;
    wsctx.handler = [&server, error_type](lws* wsi, std::span<const std::byte> payload) -> void {
        auto& session = *std::bit_cast<Session*>(ws::server::wsi_to_userdata(wsi));
        if(server.verbose) {
            PRINT("session ", &session, ": ", "received ", payload.size(), " bytes");
        }
        if(!session.handle_payload(payload)) {
            WARN("payload handling failed");

            const auto& header_o = p2p::proto::extract_header(payload);
            if(!header_o) {
                WARN("packet too short");
                assert_n(server.send_to(wsi, error_type, 0));
            } else {
                assert_n(server.send_to(wsi, error_type, header_o->id));
            }
        }
    };
    wsctx.session_data_initer = std::move(session_initer);
    wsctx.verbose             = args.websocket_verbose;
    wsctx.dump_packets        = args.websocket_dump_packets;
    ws::set_log_level(args.libws_debug_bitmap);
    assert_b(wsctx.init({
        .protocol    = protocol,
        .cert        = nullptr,
        .private_key = nullptr,
        .port        = args.port,
    }));
    print("ready");
    while(wsctx.state == ws::server::State::Connected) {
        wsctx.process();
    }
    return true;
}
