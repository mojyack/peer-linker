#include <thread>

#include "macros/assert.hpp"
#include "p2p/ice-session.hpp"

namespace {
// const auto server_domain = "saver-local-jitsi";
// const auto server_port   = 80;
const auto server_domain = "localhost";
const auto server_port   = 8080;

class ClientSession : public p2p::ice::IceSession {
    auto auth_peer(std::string_view peer_name) -> bool override {
        return peer_name == "agent a";
    }
};

auto main(bool a) -> bool {
    auto       session     = ClientSession();
    const auto peer_linker = p2p::wss::ServerLocation{server_domain, server_port};
    const auto stun_server = p2p::wss::ServerLocation{"stun.l.google.com", 19302};
    assert_b(session.start({.peer_linker     = peer_linker,
                            .stun_server     = stun_server,
                            .pad_name        = a ? "agent a" : "agent b",
                            .target_pad_name = a ? "agent b" : ""}));
    return true;
}

auto run() -> bool {
    auto t2 = std::thread(main, false);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto t1 = std::thread(main, true);
    t2.join();
    t1.join();

    return true;
}
} // namespace

auto main(const int argc, const char* argv[]) -> int {
    if(argc < 2) {
        return run() ? 0 : 1;
    } else if(argv[1][0] == 'a') {
        return main(true) ? 0 : 1;
    } else if(argv[1][0] == 'b') {
        return main(false) ? 0 : 1;
    } else {
        return run() ? 0 : 1;
    }
}
