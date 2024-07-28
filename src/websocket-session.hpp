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
    uint32_t            packet_id = 0;

    std::atomic_bool disconnected = false;

    auto handle_raw_packet(std::span<const std::byte> payload) -> void;

  protected:
    Events events;

    virtual auto get_error_packet_type() const -> uint16_t                      = 0;
    virtual auto on_packet_received(std::span<const std::byte> payload) -> bool = 0;
    virtual auto on_disconnected() -> void;

    auto is_connected() const -> bool;
    auto add_event_handler(uint32_t kind, std::function<EventHandler> handler) -> void;
    // all subclasses must call destroy in their destructor
    auto destroy() -> void;

  public:
    bool verbose = false;

    auto start(ServerLocation server, std::string protocol, const char* bind_address = nullptr) -> bool;
    auto stop() -> void;

    auto allocate_packet_id() -> uint32_t {
        return packet_id += 1;
    }

    template <class... Args>
    auto send_packet(uint16_t type, Args... args) -> bool {
        if(disconnected) {
            return false;
        }

        auto event   = Event();
        auto result  = bool();
        auto handler = std::function<EventHandler>([&event, &result](uint32_t value) {
            result = bool(value);
            event.notify();
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

        const auto id = allocate_packet_id();
        events.add_handler({
            .kind    = EventKind::Result,
            .id      = id,
            .handler = handler,
        });
        websocket_context.send(proto::build_packet(type, id, std::forward<Args>(args)...));
    }

    template <class... Args>
    auto send_result(uint16_t type, uint16_t id, Args... args) -> void {
        websocket_context.send(proto::build_packet(type, id, std::forward<Args>(args)...));
    }

    template <class... Args>
    auto send_generic_packet(uint16_t type, uint16_t id, Args... args) -> void {
        websocket_context.send(proto::build_packet(type, id, std::forward<Args>(args)...));
    }

    virtual ~WebSocketSession(){};
};
} // namespace p2p::wss
