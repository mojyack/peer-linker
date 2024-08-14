#include "peer-linker-session.hpp"
#include "macros/unwrap.hpp"
#include "peer-linker-protocol.hpp"

namespace p2p::plink {
auto PeerLinkerSession::on_pad_created() -> void {
    PRINT("pad created");
}

auto PeerLinkerSession::get_auth_secret() -> std::vector<std::byte> {
    return {};
}

auto PeerLinkerSession::auth_peer(const std::string_view /*peer_name*/, const std::span<const std::byte> /*secret*/) -> bool {
    return false;
}

auto PeerLinkerSession::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case proto::Type::Unlinked:
        stop();
        return true;
    case proto::Type::LinkAuth: {
        unwrap_pb(packet, p2p::proto::extract_payload<proto::LinkAuth>(payload));
        const auto requester_name = std::string_view(std::bit_cast<char*>(payload.data() + sizeof(proto::Link)), packet.requester_name_len);
        const auto secret         = std::span(payload.data() + sizeof(proto::Link) + packet.requester_name_len, packet.secret_len);

        const auto ok = auth_peer(requester_name, secret);
        if(verbose) {
            PRINT("received link request from name: ", requester_name);
            PRINT(ok ? "accepting peer" : "denying peer");
        }
        send_packet_detached(
            proto::Type::LinkAuthResponse, [this](const uint32_t result) {
                events.invoke(EventKind::Linked, no_id, result);
            },
            uint16_t(ok), requester_name);
        return true;
    }
    case proto::Type::LinkSuccess:
        events.invoke(EventKind::Linked, no_id, 1);
        return true;
    case proto::Type::LinkDenied:
        WARN("pad link authentication denied");
        stop();
        return true;
    default:
        return wss::WebSocketSession::on_packet_received(payload);
    }
}

auto PeerLinkerSession::start(const PeerLinkerSessionParams& params) -> bool {
    assert_b(wss::WebSocketSession::start({
        .server       = params.peer_linker,
        .ssl_level    = params.peer_linker_allow_self_signed ? ws::client::SSLLevel::TrustSelfSigned : ws::client::SSLLevel::Enable,
        .protocol     = "peer-linker",
        .bind_address = params.bind_address,
    }));
    assert_b(start_plink(params));
    return true;
}

auto PeerLinkerSession::start_plink(const PeerLinkerSessionParams& params) -> bool {
    assert_b(send_packet(::p2p::proto::Type::ActivateSession, params.user_certificate));
    assert_b(send_packet(proto::Type::Register, params.pad_name));
    on_pad_created();

    const auto controlled = params.target_pad_name.empty();
    if(!controlled) {
        const auto secret = get_auth_secret();
        assert_b(send_packet(proto::Type::Link,
                             uint16_t(params.target_pad_name.size()),
                             uint16_t(secret.size()),
                             params.target_pad_name,
                             secret));
    }
    linked_event.wait();

    return is_connected();
}

PeerLinkerSession::PeerLinkerSession() {
    add_event_handler(EventKind::Linked, [this](const uint32_t result) {
        if(!result) {
            stop();
        }
        linked_event.notify();
    });
}

PeerLinkerSession::~PeerLinkerSession() {
    destroy();
}
} // namespace p2p::plink
