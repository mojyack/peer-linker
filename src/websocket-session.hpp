#pragma once
#include <thread>

#include "event-manager.hpp"
#include "protocol-helper.hpp"
#include "util/event.hpp"
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

class WebSocketSession {
  private:
    ws::client::Context websocket_context;
    std::thread         signaling_worker;
    uint32_t            packet_id;

    std::atomic_bool disconnected = false;

  protected:
    Events events;

    virtual auto on_packet_received(std::span<const std::byte> payload) -> void = 0;
    virtual auto on_disconnected() -> void;

    auto is_connected() const -> bool;
    auto add_event_handler(uint32_t kind, std::function<EventHandler> handler) -> void;

  public:
    auto start(ServerLocation server, std::string protocol) -> bool;
    auto stop() -> void;

    template <class... Args>
    auto send_packet(uint16_t type, Args... args) -> bool {
        if(disconnected) {
            return false;
        }

        auto event   = Event();
        auto result  = bool();
        auto handler = std::function<EventHandler>([&event, &result](uint32_t value) {
            event.notify();
            result = bool(value);
        });

        send_packet_detached(type, handler, std::forward<Args>(args)...);

        event.wait();
        return result && !disconnected;
    }

    template <class... Args>
    auto send_packet_detached(uint16_t type, std::function<EventHandler> handler, Args... args) -> void {
        if(disconnected) {
            return;
        }

        const auto id = packet_id += 1;
        events.add_handler({
            .kind    = EventKind::Result,
            .id      = id,
            .handler = handler,
        });
        proto::send_packet(websocket_context.wsi, type, id, std::forward<Args>(args)...);
    }

    auto send_result(uint16_t type, uint16_t id) -> void {
        proto::send_packet(websocket_context.wsi, type, id);
    }

    virtual ~WebSocketSession();
};
} // namespace p2p::wss
