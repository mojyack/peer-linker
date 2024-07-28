#pragma once
#include <juice/juice.h>

#include "peer-linker-session.hpp"

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

class IceSession : public plink::PeerLinkerSession {
  private:
    AutoJuiceAgent agent;

  protected:
    virtual auto on_packet_received(std::span<const std::byte> payload) -> bool override;

  public:
    // internal use
    auto on_p2p_connected_state(bool flag) -> void;
    auto on_p2p_new_candidate(std::string_view sdp) -> void;
    auto on_p2p_gathering_done() -> void;

    // api
    virtual auto on_p2p_packet_received(std::span<const std::byte> payload) -> void;

    auto start(const plink::PeerLinkerSessionParams& params) -> bool;
    auto send_packet_p2p(const std::span<const std::byte> payload) -> bool;

    virtual ~IceSession() {}
};
} // namespace p2p::ice
