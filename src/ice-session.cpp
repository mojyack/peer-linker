#include "ice-session.hpp"
#include "macros/unwrap.hpp"

namespace p2p::ice {
namespace {
auto on_state_changed(juice_agent_t* const /*agent*/, const juice_state_t state, void* const user_ptr) -> void {
    PRINT("state changed: ", juice_state_to_string(state));
    auto& session = *std::bit_cast<IceSession*>(user_ptr);
    switch(state) {
    case JUICE_STATE_COMPLETED:
        session.on_p2p_connected_state(true);
        break;
    case JUICE_STATE_FAILED:
        session.on_p2p_connected_state(false);
        break;
    default:
        break;
    }
}

auto on_candidate(juice_agent_t* const /*agent*/, const char* const sdp, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_new_candidate(std::string_view(sdp));
}

auto on_gathering_done(juice_agent_t* const /*agent*/, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_gathering_done();
}

auto on_recv(juice_agent_t* const /*agent*/, const char* const data, const size_t size, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_packet_received({(std::byte*)data, size});
}
} // namespace

auto IceSession::get_error_packet_type() const -> uint16_t {
    return plink::proto::Type::Error;
}

auto IceSession::on_pad_created() -> void {
    PRINT("pad created");
}

auto IceSession::auth_peer(const std::string_view /*peer_name*/) -> bool {
    return false;
}

auto IceSession::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case plink::proto::Type::Success:
        events.invoke(wss::EventKind::Result, header.id, 1);
        return true;
    case plink::proto::Type::Error:
        events.invoke(wss::EventKind::Result, header.id, 0);
        return true;
    case plink::proto::Type::Unlinked:
        stop();
        return true;
    case plink::proto::Type::LinkAuth: {
        const auto requester_name = p2p::proto::extract_last_string<plink::proto::LinkAuth>(payload);
        PRINT("received link request from name: ", requester_name);
        const auto ok = auth_peer(requester_name);
        PRINT(ok ? "accepting peer" : "denying peer");
        send_packet_detached(
            plink::proto::Type::LinkAuthResponse, [this](const uint32_t result) {
                events.invoke(EventKind::Linked, no_id, result);
            },
            uint16_t(ok), requester_name);
        return true;
    }
    case plink::proto::Type::LinkSuccess:
        events.invoke(EventKind::Linked, no_id, 1);
        return true;
    case plink::proto::Type::LinkDenied:
        WARN("pad link authentication denied");
        stop();
        return true;
    case proto::Type::SetCandidates: {
        const auto sdp = p2p::proto::extract_last_string<proto::SetCandidates>(payload);
        PRINT("received remote candidates: ", sdp);
        juice_set_remote_description(agent.get(), sdp.data());
        events.invoke(EventKind::SDPSet, no_id, no_value);

        send_result(plink::proto::Type::Success, header.id);
        return true;
    }
    case proto::Type::AddCandidates: {
        const auto sdp = p2p::proto::extract_last_string<proto::AddCandidates>(payload);
        PRINT("received additional candidates: ", sdp);
        juice_add_remote_candidate(agent.get(), sdp.data());

        send_result(plink::proto::Type::Success, header.id);
        return true;
    }
    case proto::Type::GatheringDone: {
        PRINT("received gathering done");
        juice_set_remote_gathering_done(agent.get());
        events.invoke(EventKind::RemoteGatheringDone, no_id, no_value);

        send_result(plink::proto::Type::Success, header.id);
        return true;
    }
    default:
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto IceSession::on_p2p_connected_state(const bool flag) -> void {
    if(flag) {
        events.invoke(EventKind::Connected, no_id, no_value);
    } else {
        stop();
    }
}

auto IceSession::on_p2p_new_candidate(const std::string_view sdp) -> void {
    PRINT("new candidate: ", sdp);
    send_packet_detached(
        proto::Type::AddCandidates, [](uint32_t result) { assert_n(result, "failed to send new candidate"); }, sdp);
}

auto IceSession::on_p2p_gathering_done() -> void {
    PRINT("gathering done");
    send_packet_detached(
        proto::Type::GatheringDone, [](uint32_t result) { assert_n(result, "failed to send gathering done signal"); });
}

auto IceSession::on_p2p_packet_received(const std::span<const std::byte> payload) -> void {
    PRINT("p2p data received: ", payload.size(), " bytes");
}

auto IceSession::start(const wss::ServerLocation peer_linker, const std::string_view pad_name, const std::string_view target_pad_name, const wss::ServerLocation stun_server) -> bool {
    assert_b(wss::WebSocketSession::start(peer_linker, "peer-linker"));

    struct Events {
        Event linked;
        Event sdp_set;
        Event gathering_done;
        Event connected;
    };
    auto events = std::shared_ptr<Events>(new Events());

    add_event_handler(EventKind::Linked, [this, events](const uint32_t result) {
        if(!result) {
            stop();
        }
        events->linked.notify();
    });
    add_event_handler(EventKind::SDPSet, [events](uint32_t) { events->sdp_set.notify(); });
    add_event_handler(EventKind::RemoteGatheringDone, [events](uint32_t) { events->gathering_done.notify(); });
    add_event_handler(EventKind::Connected, [events](uint32_t) { events->connected.notify(); });

    assert_b(send_packet(plink::proto::Type::Register, pad_name));
    on_pad_created();

    const auto controlled = target_pad_name.empty();
    if(!controlled) {
        assert_b(send_packet(plink::proto::Type::Link, target_pad_name));
    }
    events->linked.wait();

    auto config = juice_config_t{
        .stun_server_host  = stun_server.address.data(),
        .stun_server_port  = stun_server.port,
        .cb_state_changed  = on_state_changed,
        .cb_candidate      = on_candidate,
        .cb_gathering_done = on_gathering_done,
        .cb_recv           = on_recv,
        .user_ptr          = this,
    };
    if(controlled) {
        config.local_port_range_begin = 60000;
        config.local_port_range_end   = 61000;
    }
    agent.reset(juice_create(&config));
    if(controlled) {
        events->sdp_set.wait();
    }

    auto sdp = std::array<char, JUICE_MAX_SDP_STRING_LEN>();
    assert_b(juice_get_local_description(agent.get(), sdp.data(), sdp.size()) == JUICE_ERR_SUCCESS);
    PRINT(pad_name, " sdp: ", sdp.data());
    assert_b(send_packet(proto::Type::SetCandidates, std::string_view(sdp.data())));

    juice_gather_candidates(agent.get());
    events->gathering_done.wait();
    events->connected.wait();
    return is_connected();
}

auto IceSession::send_packet_p2p(const std::span<const std::byte> payload) -> bool {
    assert_b(juice_send(agent.get(), (const char*)payload.data(), payload.size()) == 0);
    return true;
}

IceSession::~IceSession() {
    destroy();
}
} // namespace p2p::ice
