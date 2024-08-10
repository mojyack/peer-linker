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

class ClientSession : public p2p::ice::IceSession {
    auto get_auth_secret() -> std::vector<std::byte> override {
        auto secret = std::vector<std::byte>(strlen("password") + 1);
        std::strcpy((char*)secret.data(), "password");
        return secret;
    }

    auto auth_peer(std::string_view peer_name, std::span<const std::byte> secret) -> bool override {
        return peer_name == "agent a" && std::strcmp((char*)secret.data(), "password") == 0;
    }
};

auto main(const bool controlling) -> bool {
    auto session    = ClientSession();
    session.verbose = true;
    session.set_ws_debug_flags(true, true);
    const auto peer_linker = p2p::wss::ServerLocation{server_domain, server_port};
    const auto stun_server = p2p::wss::ServerLocation{"stun.l.google.com", 19302};
    assert_b(session.start({
                               .stun_server = stun_server,
                           },
                           {
                               .peer_linker                   = peer_linker,
                               .pad_name                      = controlling ? "agent a" : "agent b",
                               .target_pad_name               = controlling ? "agent b" : "",
                               .user_certificate              = user_cert,
                               .peer_linker_allow_self_signed = allow_self_signed,
                           }));
    return true;
}

auto run(const int argc, const char* const* const argv) -> bool {
    auto role      = "both";
    auto cert_file = (const char*)(nullptr);
    auto help      = false;

    auto parser = args::Parser<uint16_t, uint8_t>();
    parser.kwarg(&help, {"-h", "--help"}, {.arg_desc = "print this help message", .state = args::State::Initialized, .no_error_check = true});
    parser.kwarg(&cert_file, {"-k"}, {"CERT_FILE", "use user certificate", args::State::Initialized});
    parser.kwarg(&allow_self_signed, {"-a"}, {"", "allow self signed ssl certificate", args::State::Initialized});
    parser.kwarg(&role, {"-r", "--role"}, {"ROLE(both|server|client)", "test target", args::State::DefaultValue});
    if(!parser.parse(argc, argv) || help) {
        print("usage: peer-linker-test ", parser.get_help());
        return true;
    }

    if(cert_file != nullptr) {
        unwrap_ob(cert, read_file(cert_file));
        user_cert = from_span(cert);
    }

    if(const auto r = std::string_view(role); r == "both") {
        auto t2 = std::thread(main, false);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto t1 = std::thread(main, true);
        t2.join();
        t1.join();
    } else if(r == "server") {
        assert_b(main(false));
    } else if(r == "client") {
        assert_b(main(true));
    } else {
        assert_b(false, "invalid role");
    }

    return true;
}
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    return run(argc, argv) ? 0 : 1;
}
