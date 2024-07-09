#include "ice.hpp"
#include "macros/unwrap.hpp"
#include "peer-linker-protocol.hpp"
#include "util/assert.hpp"

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

auto IceSession::on_p2p_connected_state(const bool flag) -> void {
    if(flag) {
        events.invoke(EventKind::Connected, no_id, no_value);
    } else {
        stop();
    }
}

auto IceSession::on_p2p_new_candidate(const std::string_view sdp) -> void {
    PRINT("new candidate: ", sdp);
    send_packet_relayed_detached(
        plink::proto::Type::AddCandidates, [](uint32_t result) { assert_n(result, "failed to send new candidate"); }, sdp);
}

auto IceSession::on_p2p_gathering_done() -> void {
    PRINT("gathering done");
    send_packet_relayed_detached(
        plink::proto::Type::GatheringDone, [](uint32_t result) { assert_n(result, "failed to send gathering done signal"); });
}

auto IceSession::add_event_handler(const uint32_t kind, std::function<EventHandler> handler) -> void {
    events.add_handler({
        .kind    = kind,
        .id      = no_id,
        .handler = handler,
    });
}

auto IceSession::is_connected() const -> bool {
    return !disconnected;
}

auto IceSession::auth_peer(const std::string_view /*peer_name*/) -> bool {
    return false;
}

auto IceSession::handle_payload(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, proto::extract_header(payload));

    switch(header.type) {
    case plink::proto::Type::Success:
        events.invoke(EventKind::Result, header.id, 1);
        return true;
    case plink::proto::Type::Error:
        events.invoke(EventKind::Result, header.id, 0);
        return true;
    case plink::proto::Type::Unlinked:
        stop();
        return true;
    case plink::proto::Type::LinkAuth: {
        const auto requester_name = proto::extract_last_string<plink::proto::LinkAuth>(payload);
        PRINT("received link request from name: ", requester_name);
        const auto ok = auth_peer(requester_name);
        PRINT(ok ? "accepting peer" : "denying peer");
        send_packet_relayed_detached(
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
    case plink::proto::Type::SetCandidates: {
        const auto sdp = proto::extract_last_string<plink::proto::SetCandidates>(payload);
        PRINT("received remote candidates: ", sdp);
        juice_set_remote_description(agent.get(), sdp.data());
        events.invoke(EventKind::SDPSet, no_id, no_value);

        send_result_relayed(true, header.id);
        return true;
    }
    case plink::proto::Type::AddCandidates: {
        const auto sdp = proto::extract_last_string<plink::proto::AddCandidates>(payload);
        PRINT("received additional candidates: ", sdp);
        juice_add_remote_candidate(agent.get(), sdp.data());

        send_result_relayed(true, header.id);
        return true;
    }
    case plink::proto::Type::GatheringDone: {
        PRINT("received gathering done");
        juice_set_remote_gathering_done(agent.get());
        events.invoke(EventKind::RemoteGatheringDone, no_id, no_value);

        send_result_relayed(true, header.id);
        return true;
    }
    default:
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto IceSession::on_p2p_packet_received(const std::span<const std::byte> payload) -> void {
    PRINT("p2p data received: ", payload.size(), " bytes");
}

auto IceSession::on_disconnected() -> void {
    PRINT("session disconnected");
}

auto IceSession::start(const char* const server, const uint16_t port, const std::string_view pad_name, const std::string_view target_pad_name, const char* turn_server, uint16_t turn_port) -> bool {
    websocket_context.handler = [this](std::span<const std::byte> payload) -> void {
        PRINT("received ", payload.size(), " bytes");
        if(!handle_payload(payload)) {
            WARN("payload handling failed");
            send_packet_relayed(plink::proto::Type::Error); // TODO: set correct packet id
        }
    };
    websocket_context.dump_packets = true;
    assert_b(websocket_context.init(server, port, "/", "message", ws::client::SSLLevel::NoSSL));
    signaling_worker = std::thread([this]() -> void {
        while(!disconnected && websocket_context.state == ws::client::State::Connected) {
            websocket_context.process();
        }
        stop();
    });

    auto linked_event         = Event();
    auto sdp_set_event        = Event();
    auto gathering_done_event = Event();
    auto connected_event      = Event();
    add_event_handler(EventKind::Linked, [this, &linked_event](const uint32_t result) {
        if(!result) {
            stop();
        }
        linked_event.notify();
    });
    add_event_handler(EventKind::SDPSet, [&](uint32_t) { sdp_set_event.notify(); });
    add_event_handler(EventKind::RemoteGatheringDone, [&](uint32_t) { gathering_done_event.notify(); });
    add_event_handler(EventKind::Connected, [&](uint32_t) { connected_event.notify(); });

    assert_b(send_packet_relayed(plink::proto::Type::Register, pad_name));

    const auto controlled = target_pad_name.empty();
    if(!controlled) {
        assert_b(send_packet_relayed(plink::proto::Type::Link, target_pad_name));
    }
    linked_event.wait();

    auto config = juice_config_t{
        .stun_server_host  = turn_server,
        .stun_server_port  = turn_port,
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
        sdp_set_event.wait();
    }

    auto sdp = std::array<char, JUICE_MAX_SDP_STRING_LEN>();
    assert_b(juice_get_local_description(agent.get(), sdp.data(), sdp.size()) == JUICE_ERR_SUCCESS);
    PRINT(pad_name, " sdp: ", sdp.data());
    assert_b(send_packet_relayed(plink::proto::Type::SetCandidates, std::string_view(sdp.data())));

    juice_gather_candidates(agent.get());
    gathering_done_event.wait();
    connected_event.wait();
    return true;
}

auto IceSession::stop() -> void {
    if(disconnected.exchange(true)) {
        return;
    }
    events.drain();
    if(websocket_context.state == ws::client::State::Connected) {
        websocket_context.shutdown();
    }
    on_disconnected();
}

auto IceSession::send_packet_p2p(const std::span<const std::byte> payload) -> bool {
    assert_b(juice_send(agent.get(), (const char*)payload.data(), payload.size()) == 0);
    return true;
}

auto IceSession::send_result_relayed(const bool result, const uint16_t packet_id) -> void {
    const auto type = result ? plink::proto::Type::Success : plink::proto::Type::Error;
    proto::send_packet(websocket_context.wsi, type, packet_id);
}

IceSession::~IceSession() {
    stop();
    if(signaling_worker.joinable()) {
        signaling_worker.join();
    }
}
} // namespace p2p::ice
