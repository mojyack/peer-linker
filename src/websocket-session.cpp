#include "websocket-session.hpp"
#include "macros/unwrap.hpp"

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
        bail("unhandled payload type ", int(header.type));
    }
}

auto WebSocketSession::handle_raw_packet(std::span<const std::byte> payload) -> void {
    if(!on_packet_received(payload)) {
        line_warn("payload handling failed");

        const auto& header_o = p2p::proto::extract_header(payload);
        if(!header_o) {
            line_warn("packet too short");
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
    unwrap(value, wait_for_event(EventKind::Result, id));
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
    line_print("session disconnected");
}

auto WebSocketSession::is_connected() const -> bool {
    return !events.is_drained();
}

auto WebSocketSession::wait_for_event(const uint32_t kind, const uint32_t id) -> std::optional<uint32_t> {
    unwrap(result, events.wait_for(kind, id));
    ensure(result != drained_value);
    return result;
}

auto WebSocketSession::destroy() -> void {
    stop();
    if(signaling_worker.joinable()) {
        signaling_worker.join();
    }
}

auto WebSocketSession::start(const WebSocketSessionParams& params) -> bool {
    websocket_context.handler = [this](std::span<const std::byte> payload) -> void {
        if(verbose) {
            line_print("received ", payload.size(), " bytes");
        }
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

auto WebSocketSession::set_ws_debug_flags(const bool verbose, const bool dump_packets) -> void {
    websocket_context.verbose      = verbose;
    websocket_context.dump_packets = dump_packets;
}
} // namespace p2p::wss
