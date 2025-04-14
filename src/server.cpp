#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "macros/logger.hpp"
#include "net/enc/server.hpp"
#include "net/tcp/server.hpp"
#include "protocol.hpp"
#include "server.hpp"
#include "util/argument-parser.hpp"
#include "util/cleaner.hpp"
#include "util/file-io.hpp"

#define CUTIL_MACROS_PRINT_FUNC(...) LOG_ERROR(logger, __VA_ARGS__)
#include "macros/unwrap.hpp"

#if defined(_WIN32)
#include "spawn/process-win.hpp"
#else
#include "spawn/process.hpp"
#endif

namespace plink {
namespace {
auto verify_user_cert(Server& server, const std::string_view cert) -> bool {
    auto& logger = server.logger;

    auto& key = server.session_key;
    if(!key) {
        return true;
    }
    unwrap(parsed, key->split_user_certificate_to_hash_and_content(cert));
    const auto [hash_str, content] = parsed;
    ensure(key->verify_user_certificate_hash(hash_str, content));

    auto& verifier = server.user_cert_verifier;
    if(verifier.empty()) {
        return true;
    }
    auto cont = std::string(content);
    auto args = std::vector<const char*>{verifier.data(), cont.data(), nullptr};

    auto process      = process::Process();
    auto on_output    = [](const std::span<const char> output) { std::print("verifier: {}", std::string_view(output.data(), output.size())); };
    process.on_stdout = on_output;
    process.on_stderr = on_output;
    ensure(process.start({.argv = args, .die_on_parent_exit = true}), "failed to launch verifier");
    while(process.get_status() == process::Status::Running) {
        process.collect_outputs();
    }
    unwrap(result, process.join());
    ensure(result.reason == process::Result::ExitReason::Exit, "verifier exitted abnormally");
    ensure(result.code == 0, "verifier returned non-zero code {}", result.code);

    return true;
}

} // namespace
auto Session::handle_activation(const net::BytesRef payload, Server& server) -> bool {
    auto& logger = server.logger;

    unwrap(request, (serde::load<net::BinaryFormat, proto::ActivateSession>(payload)));

    LOG_INFO(logger, "received activate session");
    ensure(verify_user_cert(server, request.user_certificate), "failed to verify user certificate");
    activated = true;
    LOG_INFO(logger, "session activated");

    return true;
}

auto run(const int argc, const char* const* const argv, uint16_t port, Server& server, const std::string_view name) -> bool {
    auto session_key_secret_file = (const char*)(nullptr);
    auto user_cert_verifier      = (const char*)(nullptr);
    {
        auto parser = args::Parser<uint16_t, uint8_t>();
        auto help   = false;
        parser.kwflag(&help, {"-h", "--help"}, "print this help message", {.no_error_check = true});
        parser.kwarg(&port, {"-p"}, "PORT", "port number to use", {.state = args::State::DefaultValue});
        parser.kwarg(&session_key_secret_file, {"-k", "--key"}, "FILE", "enable user verification with the secret file", {.state = args::State::Initialized});
        parser.kwarg(&user_cert_verifier, {"-c", "--cert-verifier"}, "EXEC", "full-path of executable to verify user certificate", {.state = args::State::Initialized});
        if(!parser.parse(argc, argv) || help) {
            std::println("usage: {} {}", name, parser.get_help());
            std::exit(0);
        }
    }
    auto& logger = server.logger;
    if(session_key_secret_file != nullptr) {
        unwrap(secret, read_file(session_key_secret_file), "failed to read session key secret file");
        server.session_key.emplace(secret);
    }
    if(user_cert_verifier != nullptr) {
        server.user_cert_verifier = std::filesystem::absolute(user_cert_verifier).string();
    }

    // setup network backend
    const auto backend    = new net::enc::ServerBackendEncAdaptor();
    backend->alloc_client = [&server, &logger](net::ClientData& client) -> coop::Async<void> {
        auto cleaner = Cleaner{[&server] { server.mutex.unlock(); }};
        co_await server.mutex.lock();

        const auto ptr        = co_await server.alloc_session();
        ptr->parser.send_data = [&server, &client, &logger](net::BytesRef data) -> coop::Async<bool> {
            const auto error_value = false;
            co_ensure_v(co_await server.backend->send(client, data));
            co_return true;
        };
        client.data = ptr;
    };
    backend->free_client = [&server](void* ptr) -> coop::Async<void> {
        auto cleaner = Cleaner{[&server] { server.mutex.unlock(); }};
        co_await server.mutex.lock();
        co_await server.free_session(std::bit_cast<Session*>(ptr));
    };
    backend->on_received = [&server](const net::ClientData& client, net::BytesRef data) -> coop::Async<void> {
        auto cleaner = Cleaner{[&server] { server.mutex.unlock(); }};
        co_await server.mutex.lock();

        auto& session = *std::bit_cast<Session*>(client.data);
        if(const auto p = session.parser.parse_received(data)) {
            if(!co_await session.handle_payload(p->header, p->payload) && p->header.type != proto::Error::pt /*do not reply to error packet*/) {
                co_await session.parser.send_packet(proto::Error(), p->header.id);
            }
        }
        co_return;
    };
    server.backend.reset(backend);

    // run
    auto runner = coop::Runner();
    runner.push_task(backend->start(new net::tcp::TCPServerBackend(), port));
    runner.run();

    return true;
}
} // namespace plink
