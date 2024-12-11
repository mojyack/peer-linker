#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "macros/logger.hpp"
#include "protocol-helper.hpp"
#include "server.hpp"
#include "util/argument-parser.hpp"
#include "util/file-io.hpp"
#include "ws/misc.hpp"

#define CUTIL_MACROS_PRINT_FUNC(...) LOG_ERROR(logger, __VA_ARGS__)
#include "macros/unwrap.hpp"

#if defined(_WIN32)
#include "spawn/process-win.hpp"
#else
#include "spawn/process.hpp"
#endif

auto Session::activate(Server& server, const std::string_view cert) -> bool {
    auto& logger = server.logger;
    if(auto& key = server.session_key) {
        unwrap(parsed, key->split_user_certificate_to_hash_and_content(cert));
        const auto [hash_str, content] = parsed;
        ensure(key->verify_user_certificate_hash(hash_str, content));

        if(!server.user_cert_verifier.empty()) {
            auto cont = std::string(content);
            auto args = std::vector<const char*>{server.user_cert_verifier.data(), cont.data(), nullptr};

            auto process      = process::Process();
            auto on_output    = [](const std::span<const char> output) { std::cout << "verifier: " << std::string_view(output.data(), output.size()); };
            process.on_stdout = on_output;
            process.on_stderr = on_output;
            ensure(process.start({.argv = args, .die_on_parent_exit = true}), "failed to launch verifier");
            while(process.get_status() == process::Status::Running) {
                process.collect_outputs();
            }
            unwrap(result, process.join());
            ensure(result.reason == process::Result::ExitReason::Exit, "verifier exitted abnormally");
            ensure(result.code == 0, "verifier returned non-zero code: ", result.code);
        }
    }
    activated = true;
    return true;
}

struct ServerArgs {
    const char* session_key_secret_file = nullptr;
    const char* user_cert_verifier      = nullptr;
    const char* ssl_cert_file           = nullptr;
    const char* ssl_key_file            = nullptr;
    uint16_t    port                    = 0;
    bool        help                    = false;
    bool        websocket_dump_packets  = false;
    uint8_t     libws_debug_bitmap      = 0b11; // LLL_ERR | LLL_WARN

    static auto parse(const int argc, const char* const* argv, std::string_view program_name, uint16_t default_port) -> std::optional<ServerArgs>;
};

auto ServerArgs::parse(const int argc, const char* const* const argv, std::string_view program_name, uint16_t default_port) -> std::optional<ServerArgs> {
    auto args   = ServerArgs{.port = default_port};
    auto parser = args::Parser<uint16_t, uint8_t>();
    parser.kwflag(&args.help, {"-h", "--help"}, "print this help message", {.no_error_check = true});
    parser.kwarg(&args.port, {"-p"}, "PORT", "port number to use", {.state = args::State::DefaultValue});
    parser.kwarg(&args.session_key_secret_file, {"-k", "--key"}, "FILE", "enable user verification with the secret file", {.state = args::State::Initialized});
    parser.kwarg(&args.user_cert_verifier, {"-c", "--cert-verifier"}, "EXEC", "full-path of executable to verify user certificate", {.state = args::State::Initialized});
    parser.kwarg(&args.ssl_cert_file, {"-sc", "--ssl-cert"}, "FILE", "ssl certificate file", {.state = args::State::Initialized});
    parser.kwarg(&args.ssl_key_file, {"-sk", "--ssl-key"}, "FILE", "ssk private key file", {.state = args::State::Initialized});
    parser.kwflag(&args.websocket_dump_packets, {"-wd"}, "dump every websocket packets");
    parser.kwarg(&args.libws_debug_bitmap, {"-wb"}, "BITMAP", "libwebsockets debug flag bitmap", {.state = args::State::DefaultValue});
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
         const char* const                                   protocol) -> bool {
    auto& logger = server.logger;
    unwrap(args, ServerArgs::parse(argc, argv, protocol, default_port));
    if(args.session_key_secret_file != nullptr) {
        unwrap(secret, read_file(args.session_key_secret_file), "failed to read session key secret file");
        server.session_key.emplace(secret);
    }
    if(args.user_cert_verifier != nullptr) {
        server.user_cert_verifier = std::filesystem::absolute(args.user_cert_verifier).string();
    }

    auto& wsctx   = server.websocket_context;
    wsctx.handler = [&server, &logger](ws::server::Client* client, std::span<const std::byte> payload) -> void {
        auto& session = *std::bit_cast<Session*>(ws::server::client_to_userdata(client));
        LOG_DEBUG(server.logger, "session ", &session, ": ", "received ", payload.size(), " bytes");
        if(!session.handle_payload(payload)) {
            LOG_ERROR(server.logger, "payload handling failed");

            const auto& header_o = p2p::proto::extract_header(payload);
            if(!header_o) {
                LOG_ERROR(server.logger, "packet too short");
                ensure_v(server.send_to(client, p2p::proto::Type::Error, 0));
            } else {
                ensure_v(server.send_to(client, p2p::proto::Type::Error, header_o->id));
            }
        }
    };
    wsctx.session_data_initer = std::move(session_initer);
    wsctx.dump_packets        = args.websocket_dump_packets;
    ws::set_log_level(args.libws_debug_bitmap);
    ensure(wsctx.init({
        .protocol    = protocol,
        .cert        = args.ssl_cert_file,
        .private_key = args.ssl_key_file,
        .port        = args.port,
    }));
    print("ready");
    while(wsctx.state == ws::server::State::Connected) {
        wsctx.process();
    }
    return true;
}
