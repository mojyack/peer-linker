#pragma once
#include <juice/juice.h>

#include "websocket-session.hpp"

namespace p2p::ice {
declare_autoptr(JuiceAgent, juice_agent_t, juice_destroy);

struct EventKind {
    enum {
        Connected = wss::EventKind::Limit,
        SDPSet,
        RemoteGatheringDone,
        Linked,

        Limit,
    };
};

class IceSession : public wss::WebSocketSession {
  private:
    AutoJuiceAgent agent;

    auto handle_payload(const std::span<const std::byte> payload) -> bool;

  protected:
    virtual auto auth_peer(std::string_view peer_name) -> bool;
    virtual auto on_packet_received(std::span<const std::byte> payload) -> void override;

  public:
    // internal use
    auto on_p2p_connected_state(bool flag) -> void;
    auto on_p2p_new_candidate(std::string_view sdp) -> void;
    auto on_p2p_gathering_done() -> void;

    // api
    virtual auto on_p2p_packet_received(std::span<const std::byte> payload) -> void;

    auto start(wss::ServerLocation peer_linker, std::string_view pad_name, std::string_view target_pad_name, wss::ServerLocation stun_server) -> bool;
    auto send_packet_p2p(const std::span<const std::byte> payload) -> bool;

    virtual ~IceSession() {}
};
} // namespace p2p::ice
