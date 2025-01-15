#include "ice-session.hpp"
#include "ice-session-protocol.hpp"
#include "macros/logger.hpp"

#define CUTIL_MACROS_PRINT_FUNC(...) LOG_ERROR(logger, __VA_ARGS__)
#include "macros/unwrap.hpp"

namespace {
auto logger = Logger("p2p_ice");
}

namespace p2p::ice {
namespace {
auto on_state_changed(juice_agent_t* const /*agent*/, const juice_state_t state, void* const user_ptr) -> void {
    LOG_DEBUG(logger, "state changed to ", juice_state_to_string(state));

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

auto on_candidate(juice_agent_t* const /*agent*/, const char* const desc, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_new_candidate(std::string_view(desc));
}

auto on_gathering_done(juice_agent_t* const /*agent*/, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_gathering_done();
}

auto on_recv(juice_agent_t* const /*agent*/, const char* const data, const size_t size, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_packet_received({(std::byte*)data, size});
}
} // namespace

auto IceSession::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case proto::Type::SessionDescription: {
        const auto desc = p2p::proto::extract_last_string<proto::SessionDescription>(payload);
        LOG_DEBUG(logger, "received session description: ", desc);
        remote_desc = desc;
        events.invoke(EventKind::SessionDescSet, no_id, no_value);

        send_result(::p2p::proto::Type::Success, header.id);
        return true;
    }
    case proto::Type::Candidate: {
        const auto desc = p2p::proto::extract_last_string<proto::Candidate>(payload);
        LOG_DEBUG(logger, "received candidates: ", desc);
        juice_add_remote_candidate(agent.get(), desc.data());

        send_result(::p2p::proto::Type::Success, header.id);
        return true;
    }
    case proto::Type::GatheringDone: {
        LOG_DEBUG(logger, "remote gathering done");
        juice_set_remote_gathering_done(agent.get());
        // events.invoke(EventKind::RemoteGatheringDone, no_id, no_value);

        send_result(::p2p::proto::Type::Success, header.id);
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

auto IceSession::on_p2p_new_candidate(const std::string_view desc) -> void {
    LOG_DEBUG(logger, "new candidates: ", desc);
    send_packet_detached(
        proto::Type::Candidate, [](uint32_t result) { ensure_v(result, "failed to send new candidate"); }, desc);
}

auto IceSession::on_p2p_gathering_done() -> void {
    LOG_DEBUG(logger, "gathering done");
    send_packet_detached(
        proto::Type::GatheringDone, [](uint32_t result) { ensure_v(result, "failed to send gathering done signal"); });
}

auto IceSession::on_p2p_packet_received(const std::span<const std::byte> payload) -> void {
    LOG_DEBUG(logger, "p2p data received size=", payload.size());
}

auto IceSession::start(const IceSessionParams& params, const plink::PeerLinkerSessionParams& plink_params) -> bool {
    ensure(plink::PeerLinkerSession::start(plink_params));
    ensure(start_ice(params, plink_params));
    return true;
}

auto IceSession::start_ice(const IceSessionParams& params, const plink::PeerLinkerSessionParams& plink_params) -> bool {
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
        ensure(events.wait_for(EventKind::SessionDescSet));
        ensure(juice_set_remote_description(agent.get(), remote_desc.data()) == JUICE_ERR_SUCCESS);
    }

    auto desc = std::array<char, JUICE_MAX_SDP_STRING_LEN>();
    ensure(juice_get_local_description(agent.get(), desc.data(), desc.size()) == JUICE_ERR_SUCCESS);
    LOG_DEBUG(logger, "local session description: ", desc.data());
    ensure(send_packet(proto::Type::SessionDescription, std::string_view(desc.data())));
    if(!controlled) {
        ensure(events.wait_for(EventKind::SessionDescSet));
        ensure(juice_set_remote_description(agent.get(), remote_desc.data()) == JUICE_ERR_SUCCESS);
    }

    ensure(juice_gather_candidates(agent.get()) == JUICE_ERR_SUCCESS);
    // seems not mandatory
    // ensure(events.wait_for(EventKind::RemoteGatheringDone));
    ensure(events.wait_for(EventKind::Connected));
    return true;
}

auto IceSession::send_packet_p2p(const std::span<const std::byte> payload) -> SendResult {
    switch(juice_send(agent.get(), (const char*)payload.data(), payload.size())) {
    case JUICE_ERR_SUCCESS:
        return SendResult::Success;
    case JUICE_ERR_AGAIN:
        return SendResult::WouldBlock;
    case JUICE_ERR_TOO_LARGE:
        return SendResult::MessageTooLarge;
    default:
        return SendResult::UnknownError;
    }
}
} // namespace p2p::ice
