#include <cstring>
#include <thread>

#include "macros/unwrap.hpp"
#include "p2p/ice-session.hpp"
#include "util/argument-parser.hpp"
#include "util/file-io.hpp"
#include "util/span.hpp"

namespace {
const auto server_domain     = "localhost";
const auto server_port       = 8080;
auto       user_cert         = std::string();
auto       allow_self_signed = false;
auto       message_count     = 5;

class ClientSession : public p2p::ice::IceSession {
    auto get_auth_secret() -> std::vector<std::byte> override {
        static const auto password = to_span("password");

        auto secret = std::vector<std::byte>(password.size());
        std::memcpy(secret.data(), password.data(), secret.size());
        return secret;
    }

    auto auth_peer(std::string_view peer_name, std::span<const std::byte> secret) -> bool override {
        auto s = from_span(secret);
        print("secret=", s, " ", s.size(), s == "password");
        return peer_name == "agent a" && from_span(secret) == "password";
    }

    auto on_p2p_packet_received(const std::span<const std::byte> payload) -> void override {
        print("received p2p message: ", from_span(payload));
    }
};

auto main(const bool controlling) -> bool {
    auto session = ClientSession();
    session.set_ws_dump_packets(true);
    const auto peer_linker = p2p::ServerLocation{server_domain, server_port};
    const auto stun_server = p2p::ServerLocation{"stun.l.google.com", 19302};
    ensure(session.start({
                             .stun_server = stun_server,
                         },
                         {
                             .peer_linker                   = peer_linker,
                             .pad_name                      = controlling ? "agent a" : "agent b",
                             .target_pad_name               = controlling ? "agent b" : "",
                             .user_certificate              = user_cert,
                             .peer_linker_allow_self_signed = allow_self_signed,
                         }));
    for(auto i = 0; i < message_count; i += 1) {
        session.send_packet_p2p(to_span("Hello!"));
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return true;
}

auto run(const int argc, const char* const* const argv) -> bool {
    auto role      = "both";
    auto cert_file = (const char*)(nullptr);
    auto help      = false;

    auto parser = args::Parser<uint16_t, uint8_t>();
    parser.kwflag(&help, {"-h", "--help"}, "print this help message", {.no_error_check = true});
    parser.kwarg(&cert_file, {"-k"}, "CERT_FILE", "use user certificate", {.state = args::State::Initialized});
    parser.kwflag(&allow_self_signed, {"-a"}, "allow self signed ssl certificate");
    parser.kwarg(&message_count, {"-c"}, "COUNT", "number of messages to send", {.state = args::State::DefaultValue});
    parser.kwarg(&role, {"-r", "--role"}, "ROLE(both|server|client)", "test target", {.state = args::State::DefaultValue});
    if(!parser.parse(argc, argv) || help) {
        print("usage: peer-linker-test ", parser.get_help());
        return true;
    }

    if(cert_file != nullptr) {
        unwrap(cert, read_file(cert_file));
        user_cert = from_span(cert);
    }

    if(const auto r = std::string_view(role); r == "both") {
        auto t2 = std::thread(main, false);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto t1 = std::thread(main, true);
        t2.join();
        t1.join();
    } else if(r == "server") {
        ensure(main(false));
    } else if(r == "client") {
        ensure(main(true));
    } else {
        bail("invalid role");
    }

    return true;
}
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    return run(argc, argv) ? 0 : 1;
}
