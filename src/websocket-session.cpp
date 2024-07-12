#include "websocket-session.hpp"
#include "macros/assert.hpp"
#include "util/assert.hpp"

namespace p2p::wss {
auto WebSocketSession::handle_raw_packet(std::span<const std::byte> payload) -> void {
    if(!on_packet_received(payload)) {
        WARN("payload handling failed");

        const auto& header_o = p2p::proto::extract_header(payload);
        if(!header_o) {
            WARN("packet too short");
            send_result(get_error_packet_type(), 0);
        } else {
            send_result(get_error_packet_type(), header_o->id);
        }
    }
}

auto WebSocketSession::on_disconnected() -> void {
    PRINT("session disconnected");
}

auto WebSocketSession::is_connected() const -> bool {
    return !disconnected;
}

auto WebSocketSession::add_event_handler(const uint32_t kind, std::function<EventHandler> handler) -> void {
    events.add_handler({
        .kind    = kind,
        .id      = no_id,
        .handler = handler,
    });
}

auto WebSocketSession::start(const ServerLocation server, std::string protocol) -> bool {
    websocket_context.handler = [this](std::span<const std::byte> payload) -> void {
        assert_n(!disconnected);
        PRINT("received ", payload.size(), " bytes");
        on_packet_received(payload);
    };
    websocket_context.dump_packets = true;
    // TODO: enable ssl
    assert_b(websocket_context.init(server.address.data(), server.port, "/", protocol.data(), ws::client::SSLLevel::NoSSL));
    signaling_worker = std::thread([this]() -> void {
        while(!disconnected && websocket_context.state == ws::client::State::Connected) {
            websocket_context.process();
        }
        stop();
    });
    return true;
}

auto WebSocketSession::stop() -> void {
    if(disconnected.exchange(true)) {
        return;
    }
    events.drain();
    if(websocket_context.state == ws::client::State::Connected) {
        websocket_context.shutdown();
    }
    on_disconnected();
}

WebSocketSession::~WebSocketSession() {
    stop();
    if(signaling_worker.joinable()) {
        signaling_worker.join();
    }
}
} // namespace p2p::wss
