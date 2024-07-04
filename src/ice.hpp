#pragma once
#include <thread>

#include <juice/juice.h>

#include "signaling-protocol-helper.hpp"
#include "util/waiters-event.hpp"
#include "ws/client.hpp"

namespace p2p::ice {
declare_autoptr(JuiceAgent, juice_agent_t, juice_destroy);

struct EventKind {
    enum {
        None = 0,
        Connected,
        Disconnected,
        SDPSet,
        GatheringDone,
        Result,
        Linked,
    };
};

using EventHandler = void(uint32_t value);

constexpr auto ignore_id = uint32_t(-1);

struct IceEventHandlerInfo {
    uint32_t                    kind;
    uint32_t                    id;
    std::function<EventHandler> handler;
};

struct IceEvent {
    uint32_t kind;
    uint32_t id;
    uint32_t value;
};

// there is two ways of handling packet result.
// 1. use wait_for(kind, id) to block until the desired packet arrived
// 2. use add_result_handler(info) to register callback
struct IceEvents {
    WaitersEvent notifier;

    std::mutex                       lock;
    std::vector<IceEvent>            events;
    std::vector<IceEventHandlerInfo> result_handlers;

    auto set(uint32_t kind, uint32_t id = 0, uint32_t value = 0) -> void;
    auto wait_for(uint32_t kind, uint32_t id = ignore_id) -> std::optional<uint32_t>;
    auto add_result_handler(IceEventHandlerInfo info) -> void;
};

class IceSession {
  private:
    // connection to signaling server
    ws::client::Context websocket_context;
    std::thread         signaling_worker;

    // connection to another peer
    AutoJuiceAgent agent;

    // event handling
    IceEvents events;

    // packet id for signaling server
    uint32_t packet_id;

    auto handle_payload(const std::span<const std::byte> payload) -> bool;

  public:
    // internal use
    auto on_p2p_connected_state(bool flag) -> void;
    auto on_p2p_new_candidate(std::string_view sdp) -> void;
    auto on_p2p_gathering_done() -> void;

    // api
    virtual auto on_p2p_data(std::span<const std::byte> payload) -> void;
    virtual auto on_p2p_disconnected() -> void;
    virtual auto auth_peer(std::string_view peer_name) -> bool;

    auto start(const char* server, uint16_t port, std::string_view pad_name, std::string_view target_pad_name, const char* turn_server, uint16_t turn_port) -> bool;
    auto stop() -> void;
    auto wait_for_success(uint32_t packet_id) -> bool;
    auto send_payload(const std::span<const std::byte> payload) -> bool;

    template <class... Args>
    auto send_packet_relayed(uint16_t type, Args... args) -> uint32_t {
        const auto id = packet_id += 1;
        proto::send_packet(websocket_context.wsi, type, id, std::forward<Args>(args)...);
        return id;
    }

    template <class... Args>
    auto send_packet_relayed_detached(uint16_t type, std::function<EventHandler> handler, Args... args) -> void {
        const auto id = packet_id += 1;
        events.add_result_handler({
            .kind    = EventKind::Result,
            .id      = id,
            .handler = handler,
        });
        proto::send_packet(websocket_context.wsi, type, id, std::forward<Args>(args)...);
    }

    virtual ~IceSession();
};
} // namespace p2p::ice
