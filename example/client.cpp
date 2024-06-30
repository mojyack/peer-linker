#include <thread>

#include "macros/unwrap.hpp"
#include "p2p/ice.hpp"
#include "p2p/signaling-protocol-helper.hpp"
#include "util/assert.hpp"

namespace {
// const auto server_domain = "saver-local-jitsi";
// const auto server_port   = 80;
const auto server_domain = "localhost";
const auto server_port   = 8080;

struct ClientSession : p2p::ice::IceSession {
    auto handle_payload(const std::span<const std::byte> payload) -> bool override {
        unwrap_pb(header, p2p::proto::extract_header(payload));
        switch(header.type) {
        case p2p::proto::Type::LinkAuth: {
            const auto requester_name = p2p::proto::extract_last_string<p2p::proto::LinkAuth>(payload);
            PRINT("received link request from name: ", requester_name);
            p2p::proto::send_packet(websocket_context.wsi, p2p::proto::Type::LinkAuthResponse, uint16_t(requester_name == "agent a"), requester_name);
            return true;
        }
        default:
            return p2p::ice::IceSession::handle_payload(payload);
        }
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
