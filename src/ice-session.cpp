#include "ice-session.hpp"
#include "ice-session-protocol.hpp"
#include "macros/unwrap.hpp"

namespace p2p::ice {
namespace {
auto on_state_changed(juice_agent_t* const /*agent*/, const juice_state_t state, void* const user_ptr) -> void {
    auto& session = *std::bit_cast<IceSession*>(user_ptr);
    if(session.verbose) {
        PRINT("state changed: ", juice_state_to_string(state));
    }
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

auto IceSession::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case proto::Type::SetCandidates: {
        const auto sdp = p2p::proto::extract_last_string<proto::SetCandidates>(payload);
        if(verbose) {
            PRINT("received remote candidates: ", sdp);
        }
        remote_sdp = sdp;
        events.invoke(EventKind::SDPSet, no_id, no_value);

        send_result(plink::proto::Type::Success, header.id);
        return true;
    }
    case proto::Type::AddCandidates: {
        const auto sdp = p2p::proto::extract_last_string<proto::AddCandidates>(payload);
        if(verbose) {
            PRINT("received additional candidates: ", sdp);
        }
        juice_add_remote_candidate(agent.get(), sdp.data());

        send_result(plink::proto::Type::Success, header.id);
        return true;
    }
    case proto::Type::GatheringDone: {
        if(verbose) {
            PRINT("received gathering done");
        }
        juice_set_remote_gathering_done(agent.get());
        events.invoke(EventKind::RemoteGatheringDone, no_id, no_value);

        send_result(plink::proto::Type::Success, header.id);
        return true;
    }
    default:
        return plink::PeerLinkerSession::on_packet_received(payload);
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
    if(verbose) {
        PRINT("new candidate: ", sdp);
    }
    send_packet_detached(
        proto::Type::AddCandidates, [](uint32_t result) { assert_n(result, "failed to send new candidate"); }, sdp);
}

auto IceSession::on_p2p_gathering_done() -> void {
    if(verbose) {
        PRINT("gathering done");
    }
    send_packet_detached(
        proto::Type::GatheringDone, [](uint32_t result) { assert_n(result, "failed to send gathering done signal"); });
}

auto IceSession::on_p2p_packet_received(const std::span<const std::byte> payload) -> void {
    PRINT("p2p data received: ", payload.size(), " bytes");
}

auto IceSession::start(const IceSessionParams& params, const plink::PeerLinkerSessionParams& plink_params) -> bool {
    struct Events {
        Event sdp_set;
        Event gathering_done;
        Event connected;
    };
    auto events = std::shared_ptr<Events>(new Events());

    add_event_handler(EventKind::SDPSet, [events](uint32_t) { events->sdp_set.notify(); });
    add_event_handler(EventKind::RemoteGatheringDone, [events](uint32_t) { events->gathering_done.notify(); });
    add_event_handler(EventKind::Connected, [events](uint32_t) { events->connected.notify(); });

    assert_b(plink::PeerLinkerSession::start(plink_params));

    const auto controlled = plink_params.target_pad_name.empty();

    auto config = juice_config_t{
        .stun_server_host  = params.stun_server.address.data(),
        .stun_server_port  = params.stun_server.port,
        .bind_address      = plink_params.bind_address,
        .cb_state_changed  = on_state_changed,
        .cb_candidate      = on_candidate,
        .cb_gathering_done = on_gathering_done,
        .cb_recv           = on_recv,
        .user_ptr          = this,
    };
    if(!params.turn_servers.empty()) {
        config.turn_servers       = (juice_turn_server_t*)params.turn_servers.data();
        config.turn_servers_count = params.turn_servers.size();
    }
    if(controlled) {
        config.local_port_range_begin = 60000;
        config.local_port_range_end   = 61000;
    }
    agent.reset(juice_create(&config));
    if(controlled) {
        events->sdp_set.wait();
        juice_set_remote_description(agent.get(), remote_sdp.data());
    }

    auto sdp = std::array<char, JUICE_MAX_SDP_STRING_LEN>();
    assert_b(juice_get_local_description(agent.get(), sdp.data(), sdp.size()) == JUICE_ERR_SUCCESS);
    if(verbose) {
        PRINT(plink_params.pad_name, "local sdp: ", sdp.data());
    }
    assert_b(send_packet(proto::Type::SetCandidates, std::string_view(sdp.data())));
    if(!controlled) {
        events->sdp_set.wait();
        juice_set_remote_description(agent.get(), remote_sdp.data());
    }

    juice_gather_candidates(agent.get());
    // seems not mandatory
    // events->gathering_done.wait();
    events->connected.wait();
    return is_connected();
}

auto IceSession::send_packet_p2p(const std::span<const std::byte> payload) -> bool {
    assert_b(juice_send(agent.get(), (const char*)payload.data(), payload.size()) == 0);
    return true;
}
} // namespace p2p::ice
