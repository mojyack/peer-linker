#pragma once
#include <thread>

#include <juice/juice.h>

#include "event-manager.hpp"
#include "protocol-helper.hpp"
#include "util/event.hpp"
#include "ws/client.hpp"

namespace p2p::ice {
declare_autoptr(JuiceAgent, juice_agent_t, juice_destroy);

struct EventKind {
    enum {
        None = 0,
        Connected,
        SDPSet,
        RemoteGatheringDone,
        Result,
        Linked,

        Limit,
    };
};

class IceSession {
  private:
    // connection to signaling server
    ws::client::Context websocket_context;
    std::thread         signaling_worker;

    // connection to another peer
    AutoJuiceAgent agent;

    // packet id for signaling server
    uint32_t packet_id;

    std::atomic_bool disconnected = false;

  protected:
    Events events;

    auto is_connected() const -> bool;

    virtual auto auth_peer(std::string_view peer_name) -> bool;
    virtual auto handle_payload(const std::span<const std::byte> payload) -> bool;

  public:
    // internal use
    auto on_p2p_connected_state(bool flag) -> void;
    auto on_p2p_new_candidate(std::string_view sdp) -> void;
    auto on_p2p_gathering_done() -> void;
    auto add_event_handler(uint32_t kind, std::function<EventHandler> handler) -> void;

    // api
    virtual auto on_p2p_packet_received(std::span<const std::byte> payload) -> void;
    virtual auto on_disconnected() -> void;

    auto start(const char* server, uint16_t port, std::string_view pad_name, std::string_view target_pad_name, const char* turn_server, uint16_t turn_port) -> bool;
    auto stop() -> void;

    // to p2p peer
    auto send_packet_p2p(const std::span<const std::byte> payload) -> bool;

    // to signaling server
    auto send_result_relayed(bool result, uint16_t packet_id) -> void;

    template <class... Args>
    auto send_packet_relayed(uint16_t type, Args... args) -> bool {
        if(disconnected) {
            return false;
        }

        auto event   = Event();
        auto result  = bool();
        auto handler = std::function<EventHandler>([&event, &result](uint32_t value) {
            event.notify();
            result = bool(value);
        });

        send_packet_relayed_detached(type, handler, std::forward<Args>(args)...);

        event.wait();
        return result && !disconnected;
    }

    template <class... Args>
    auto send_packet_relayed_detached(uint16_t type, std::function<EventHandler> handler, Args... args) -> void {
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

    virtual ~IceSession();
};
} // namespace p2p::ice
