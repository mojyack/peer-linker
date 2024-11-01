#pragma once
#include <thread>

#include "event-manager.hpp"
#include "protocol-helper.hpp"
#include "ws/client.hpp"

namespace p2p::wss {
struct EventKind {
    enum {
        Result,

        Limit,
    };
};

struct ServerLocation {
    std::string address;
    uint16_t    port;
};

struct WebSocketSessionParams {
    ServerLocation       server;
    ws::client::SSLLevel ssl_level = ws::client::SSLLevel::Enable;
    const char*          protocol;
    const char*          bind_address = nullptr;
    ws::KeepAliveParams  keepalive    = {};
};

class WebSocketSession {
  private:
    ws::client::Context websocket_context;
    std::thread         signaling_worker;
    uint32_t            packet_id = 0;

    auto handle_raw_packet(std::span<const std::byte> payload) -> void;
    auto send_packet(std::vector<std::byte> payload) -> bool;
    auto send_packet_detached(EventCallback callback, std::vector<std::byte> payload) -> bool;

  protected:
    Events events;

    virtual auto on_packet_received(std::span<const std::byte> payload) -> bool;
    virtual auto on_disconnected() -> void;

    auto is_connected() const -> bool;
    // all subclasses must call destroy in their destructor
    auto destroy() -> void;

  public:
    auto start(const WebSocketSessionParams& params) -> bool;
    auto stop() -> void;
    auto set_ws_dump_packets(bool flag) -> void;

    auto allocate_packet_id() -> uint32_t {
        return packet_id += 1;
    }

    template <class... Args>
    auto send_packet(const uint16_t type, Args... args) -> bool {
        return send_packet(proto::build_packet(type, 0, std::forward<Args>(args)...));
    }

    template <class... Args>
    auto send_packet_detached(const uint16_t type, const EventCallback callback, Args... args) -> bool {
        return send_packet_detached(callback, proto::build_packet(type, 0, std::forward<Args>(args)...));
    }

    template <class... Args>
    auto send_result(uint16_t type, uint16_t id, Args... args) -> void {
        websocket_context.send(proto::build_packet(type, id, std::forward<Args>(args)...));
    }

    template <class... Args>
    auto send_generic_packet(uint16_t type, uint16_t id, Args... args) -> void {
        websocket_context.send(proto::build_packet(type, id, std::forward<Args>(args)...));
    }

    virtual ~WebSocketSession() {}
};
} // namespace p2p::wss
