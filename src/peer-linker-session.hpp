#pragma once
#include "websocket-session.hpp"

namespace p2p::plink {
struct EventKind {
    enum {
        Linked = wss::EventKind::Limit,

        Limit,
    };
};

struct PeerLinkerSessionParams {
    wss::ServerLocation peer_linker;
    std::string_view    pad_name;
    std::string_view    target_pad_name;
    std::string_view    user_certificate;
    const char*         bind_address                  = nullptr;
    bool                peer_linker_allow_self_signed = false;
};

class PeerLinkerSession : public wss::WebSocketSession {
  protected:
    virtual auto on_pad_created() -> void;
    virtual auto get_auth_secret() -> std::vector<std::byte>;
    virtual auto auth_peer(std::string_view peer_name, std::span<const std::byte> secret) -> bool;
    virtual auto on_packet_received(std::span<const std::byte> payload) -> bool override;

  public:
    auto start(const PeerLinkerSessionParams& params) -> bool;

    virtual ~PeerLinkerSession();
};
} // namespace p2p::plink
