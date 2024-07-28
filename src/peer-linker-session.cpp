#include "peer-linker-session.hpp"
#include "macros/unwrap.hpp"
#include "peer-linker-protocol.hpp"

namespace p2p::plink {
auto PeerLinkerSession::get_error_packet_type() const -> uint16_t {
    return proto::Type::Error;
}

auto PeerLinkerSession::on_pad_created() -> void {
    PRINT("pad created");
}

auto PeerLinkerSession::auth_peer(const std::string_view /*peer_name*/) -> bool {
    return false;
}

auto PeerLinkerSession::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case proto::Type::Success:
        events.invoke(wss::EventKind::Result, header.id, 1);
        return true;
    case proto::Type::Error:
        events.invoke(wss::EventKind::Result, header.id, 0);
        return true;
    case proto::Type::Unlinked:
        stop();
        return true;
    case proto::Type::LinkAuth: {
        const auto requester_name = p2p::proto::extract_last_string<proto::LinkAuth>(payload);
        PRINT("received link request from name: ", requester_name);
        const auto ok = auth_peer(requester_name);
        PRINT(ok ? "accepting peer" : "denying peer");
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
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto PeerLinkerSession::start(const PeerLinkerSessionParams& params) -> bool {
    assert_b(wss::WebSocketSession::start(params.peer_linker, "peer-linker", params.bind_address));

    struct Events {
        Event linked;
    };
    auto events = std::shared_ptr<Events>(new Events());

    add_event_handler(EventKind::Linked, [this, events](const uint32_t result) {
        if(!result) {
            stop();
        }
        events->linked.notify();
    });

    assert_b(send_packet(proto::Type::Register, params.pad_name));
    on_pad_created();

    const auto controlled = params.target_pad_name.empty();
    if(!controlled) {
        assert_b(send_packet(proto::Type::Link, params.target_pad_name));
    }
    events->linked.wait();

    return is_connected();
}

PeerLinkerSession::~PeerLinkerSession() {
    destroy();
}
} // namespace p2p::plink
