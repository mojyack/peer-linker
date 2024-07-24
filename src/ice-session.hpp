#pragma once
#include <juice/juice.h>

#include "peer-linker-protocol.hpp"
#include "websocket-session.hpp"

namespace p2p::ice {
namespace proto {
struct Type {
    enum : uint16_t {
        SetCandidates = ::p2p::plink::proto::Type::Limit,
        AddCandidates,
        GatheringDone,

        Limit,
    };
};

struct SetCandidates : ::p2p::proto::Packet {
    // char sdp[];
};

struct AddCandidates : ::p2p::proto::Packet {
    // char sdp[];
};

struct GatheringDone : ::p2p::proto::Packet {
};
} // namespace proto

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

struct IceSessionParams {
    wss::ServerLocation peer_linker;
    wss::ServerLocation stun_server;
    std::string_view    pad_name;
    std::string_view    target_pad_name;
    const char*         bind_address = nullptr;
};

class IceSession : public wss::WebSocketSession {
  private:
    AutoJuiceAgent agent;

    auto get_error_packet_type() const -> uint16_t override;

  protected:
    virtual auto on_pad_created() -> void;
    virtual auto auth_peer(std::string_view peer_name) -> bool;
    virtual auto on_packet_received(std::span<const std::byte> payload) -> bool override;

  public:
    // internal use
    auto on_p2p_connected_state(bool flag) -> void;
    auto on_p2p_new_candidate(std::string_view sdp) -> void;
    auto on_p2p_gathering_done() -> void;

    // api
    virtual auto on_p2p_packet_received(std::span<const std::byte> payload) -> void;

    auto start(const IceSessionParams& params) -> bool;
    auto send_packet_p2p(const std::span<const std::byte> payload) -> bool;

    virtual ~IceSession();
};
} // namespace p2p::ice
