#pragma once
#include <thread>

#include <juice/juice.h>

#include "util/event.hpp"
#include "ws/client.hpp"

namespace p2p::ice {
declare_autoptr(JuiceAgent, juice_agent_t, juice_destroy);

struct IceSession {
    // connection to signaling server
    ws::client::Context websocket_context;
    std::thread         signaling_worker;

    AutoJuiceAgent agent;
    Event          event;
    int            event_kind;
    bool           result;

    virtual auto on_p2p_data(std::span<const std::byte> payload) -> void;
    virtual auto on_disconnected() -> void;
    virtual auto handle_payload(const std::span<const std::byte> payload) -> bool;

    auto start(const char* server, uint16_t port, std::string_view pad_name, std::string_view target_pad_name, const char* turn_server, uint16_t turn_port) -> bool;
    auto stop() -> void;
    auto wait_for_success() -> bool;
    auto send_payload(const std::span<const std::byte> payload) -> bool;
    auto send_payload_relayed(const std::span<const std::byte> payload) -> bool;

    virtual ~IceSession();
};
} // namespace p2p::ice
