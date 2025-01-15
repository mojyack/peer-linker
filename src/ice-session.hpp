#pragma once
#include <juice/juice.h>

#include "peer-linker-session.hpp"

namespace p2p::ice {
declare_autoptr(JuiceAgent, juice_agent_t, juice_destroy);

struct EventKind {
    enum {
        Connected = plink::EventKind::Limit,
        SessionDescSet,
        RemoteGatheringDone,

        Limit,
    };
};

enum class SendResult {
    Success,
    WouldBlock,
    MessageTooLarge,
    UnknownError,
};

struct IceSessionParams {
    ServerLocation                   stun_server;
    std::vector<juice_turn_server_t> turn_servers;
};

class IceSession : public plink::PeerLinkerSession {
  private:
    AutoJuiceAgent agent;
    std::string    remote_desc;

  protected:
    virtual auto on_packet_received(std::span<const std::byte> payload) -> bool override;

  public:
    // internal use
    auto on_p2p_connected_state(bool flag) -> void;
    auto on_p2p_new_candidate(std::string_view desc) -> void;
    auto on_p2p_gathering_done() -> void;

    // api
    virtual auto on_p2p_packet_received(std::span<const std::byte> payload) -> void;

    auto start(const IceSessionParams& params, const plink::PeerLinkerSessionParams& plink_params) -> bool;
    auto start_ice(const IceSessionParams& params, const plink::PeerLinkerSessionParams& plink_params) -> bool;
    auto send_packet_p2p(const std::span<const std::byte> payload) -> SendResult;

    virtual ~IceSession() {}
};
} // namespace p2p::ice
