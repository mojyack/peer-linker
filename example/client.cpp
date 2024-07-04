#include <thread>

#include "macros/unwrap.hpp"
#include "p2p/ice.hpp"
#include "util/assert.hpp"

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
    auto session = ClientSession();
    assert_b(session.start(server_domain, server_port, a ? "agent a" : "agent b", a ? "agent b" : "", "stun.l.google.com", 19302));
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
