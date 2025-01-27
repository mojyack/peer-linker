#include "websocket-session.hpp"
#include "macros/logger.hpp"

#define CUTIL_MACROS_PRINT_FUNC(...) LOG_ERROR(logger, __VA_ARGS__)
#include "macros/unwrap.hpp"

namespace {
auto logger = Logger("p2p_ws");
}

namespace p2p::wss {
auto WebSocketSession::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap(header, ::p2p::proto::extract_header(payload));

    switch(header.type) {
    case ::p2p::proto::Type::Success:
        events.invoke(wss::EventKind::Result, header.id, 1);
        return true;
    case ::p2p::proto::Type::Error:
        events.invoke(wss::EventKind::Result, header.id, 0);
        return true;
    default:
        bail("unhandled payload type {}", int(header.type));
    }
}

auto WebSocketSession::handle_raw_packet(std::span<const std::byte> payload) -> void {
    if(!on_packet_received(payload)) {
        LOG_ERROR(logger, "payload handling failed");

        const auto& header_o = p2p::proto::extract_header(payload);
        if(!header_o) {
            LOG_ERROR(logger, "packet too short");
            send_result(::p2p::proto::Type::Error, 0);
        } else {
            send_result(::p2p::proto::Type::Error, header_o->id);
        }
    }
}

auto WebSocketSession::send_packet(std::vector<std::byte> payload) -> bool {
    const auto id = allocate_packet_id();

    std::bit_cast<proto::Packet*>(payload.data())->id = id;
    ensure(websocket_context.send(payload));
    unwrap(value, events.wait_for(EventKind::Result, id));
    ensure(value == 1);
    return true;
}

auto WebSocketSession::send_packet_detached(const EventCallback callback, const std::vector<std::byte> payload) -> bool {
    const auto id = allocate_packet_id();

    std::bit_cast<proto::Packet*>(payload.data())->id = id;
    ensure(events.register_callback(EventKind::Result, id, callback));
    ensure(websocket_context.send(payload));
    return true;
}

auto WebSocketSession::on_disconnected() -> void {
    LOG_INFO(logger, "session disconnected");
}

auto WebSocketSession::is_connected() const -> bool {
    return !events.is_drained();
}

auto WebSocketSession::destroy() -> void {
    stop();
    if(signaling_worker.joinable()) {
        signaling_worker.join();
    }
}

auto WebSocketSession::start(const WebSocketSessionParams& params) -> bool {
    websocket_context.handler = [this](std::span<const std::byte> payload) -> void {
        LOG_DEBUG(logger, "received {} bytes", payload.size());
        handle_raw_packet(payload);
    };
    ensure(websocket_context.init({
        .address      = params.server.address.data(),
        .path         = "/",
        .protocol     = params.protocol,
        .cert         = nullptr,
        .bind_address = params.bind_address,
        .port         = params.server.port,
        .ssl_level    = params.ssl_level,
        .keepalive    = params.keepalive,
    }));
    signaling_worker = std::thread([this]() -> void {
        while(is_connected() && websocket_context.state == ws::client::State::Connected) {
            websocket_context.process();
        }
        stop();
    });
    return true;
}

auto WebSocketSession::stop() -> void {
    if(!events.drain()) {
        return;
    }
    if(websocket_context.state == ws::client::State::Connected) {
        websocket_context.shutdown();
    }
    on_disconnected();
}

auto WebSocketSession::set_ws_dump_packets(const bool flag) -> void {
    websocket_context.dump_packets = flag;
}
} // namespace p2p::wss
