#include "ice.hpp"
#include "macros/unwrap.hpp"
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
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_data({(std::byte*)data, size});
}
} // namespace

auto IceEvents::set(uint32_t kind, const uint32_t id, const uint32_t value) -> void {
    PRINT("new event: ", kind);
    auto info = std::optional<IceEventHandlerInfo>();
    {
        auto guard = std::lock_guard(lock);

        // search from detached result handlers
        for(auto i = result_handlers.begin(); i < result_handlers.end(); i += 1) {
            if(i->kind == kind && i->id == id) {
                info = std::move(*i);
                result_handlers.erase(i);
                break;
            }
        }
        if(!info) {
            events.emplace_back(kind, id, value);
        }
    }
    if(!info) {
        if(kind == EventKind::Disconnected) {
            notifier.drain();
        } else {
            notifier.notify();
        }
    } else {
        info->handler(value);
    }
}

auto IceEvents::wait_for(const uint32_t target_kind, const uint32_t target_id) -> std::optional<uint32_t> {
    auto intent = WaitersEventIntent(notifier);
    while(true) {
        {
            auto guard = std::lock_guard(lock);
            for(auto i = events.begin(); i != events.end(); i += 1) {
                if(i->kind == EventKind::Disconnected) {
                    return std::nullopt;
                }
                if(i->kind == target_kind && (target_id == ignore_id || i->id == target_id)) {
                    const auto value = i->value;
                    events.erase(i);
                    return value;
                }
            }
        }
        notifier.wait();
    }
}

auto IceEvents::add_result_handler(IceEventHandlerInfo info) -> void {
    auto guard = std::lock_guard(lock);
    result_handlers.push_back(info);
}

auto IceSession::on_p2p_connected_state(const bool flag) -> void {
    if(flag) {
        events.set(EventKind::Connected);
    } else {
        events.set(EventKind::Disconnected);
        on_p2p_disconnected();
    }
}

auto IceSession::on_p2p_new_candidate(const std::string_view sdp) -> void {
    PRINT("new candidate: ", sdp);
    send_packet_relayed(proto::Type::AddCandidates, std::string_view(sdp));
    send_packet_relayed_detached(
        p2p::proto::Type::AddCandidates, [](uint32_t result) { assert_n(result, "sending new candidates failed"); }, sdp);
}

auto IceSession::on_p2p_gathering_done() -> void {
    PRINT("gathering done");
    events.set(EventKind::GatheringDone);
    send_packet_relayed_detached(
        p2p::proto::Type::GatheringDone, [](uint32_t result) { assert_n(result, "sending gathering done failed"); });
}

auto IceSession::handle_payload(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case proto::Type::Success:
        events.set(EventKind::Result, header.id, 1);
        return true;
    case proto::Type::Error:
        events.set(EventKind::Result, header.id, 0);
        return true;
    case proto::Type::Unlinked:
        events.set(EventKind::Disconnected);
        return true;
    case proto::Type::LinkAuth: {
        const auto requester_name = p2p::proto::extract_last_string<p2p::proto::LinkAuth>(payload);
        PRINT("received link request from name: ", requester_name);
        const auto ok = auth_peer(requester_name);
        PRINT(ok ? "accepting peer" : "denying peer");
        send_packet_relayed_detached(
            p2p::proto::Type::LinkAuthResponse, [this](const uint32_t result) {
                if(result) {
                    events.set(EventKind::Linked);
                } else {
                    WARN("failed to send auth response");
                    events.set(EventKind::Disconnected);
                }
            },
            uint16_t(ok), requester_name);
        return true;
    }
    case proto::Type::LinkSuccess:
        events.set(EventKind::Linked);
        return true;
    case proto::Type::LinkDenied:
        WARN("pad link authentication denied");
        events.set(EventKind::Disconnected);
        return true;
    case proto::Type::SetCandidates: {
        const auto sdp = proto::extract_last_string<proto::SetCandidates>(payload);
        PRINT("received remote candidates: ", sdp);
        juice_set_remote_description(agent.get(), sdp.data());
        events.set(EventKind::SDPSet);

        send_packet_relayed(proto::Type::Success);
        return true;
    }
    case proto::Type::AddCandidates: {
        const auto sdp = proto::extract_last_string<proto::AddCandidates>(payload);
        PRINT("received additional candidates: ", sdp);
        juice_add_remote_candidate(agent.get(), sdp.data());

        send_packet_relayed(proto::Type::Success);
        return true;
    }
    case proto::Type::GatheringDone: {
        PRINT("received gathering done");
        juice_set_remote_gathering_done(agent.get());

        send_packet_relayed(proto::Type::Success);
        return true;
    }
    default:
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto IceSession::on_p2p_data(const std::span<const std::byte> payload) -> void {
    PRINT("p2p data received: ", payload.size(), " bytes");
}

auto IceSession::on_p2p_disconnected() -> void {
    PRINT("session disconnected");
}

auto IceSession::auth_peer(const std::string_view /*peer_name*/) -> bool {
    return false;
}

auto IceSession::start(const char* const server, const uint16_t port, const std::string_view pad_name, const std::string_view target_pad_name, const char* turn_server, uint16_t turn_port) -> bool {
    websocket_context.handler = [this](std::span<const std::byte> payload) -> void {
        PRINT("received ", payload.size(), " bytes");
        if(!handle_payload(payload)) {
            WARN("payload handling failed");
            send_packet_relayed(proto::Type::Error);
        }
    };
    websocket_context.dump_packets = true;
    assert_b(websocket_context.init(server, port, "/", "message", ws::client::SSLLevel::NoSSL));
    signaling_worker = std::thread([this]() -> void {
        while(websocket_context.state == ws::client::State::Connected) {
            websocket_context.process();
        }
        events.set(EventKind::Disconnected);
    });

    const auto controlled = target_pad_name.empty();

    assert_b(wait_for_success(send_packet_relayed(proto::Type::Register, pad_name)));
    if(!controlled) {
        assert_b(wait_for_success(send_packet_relayed(proto::Type::Link, target_pad_name)));
        assert_b(events.wait_for(EventKind::Linked));
    } else {
        assert_b(events.wait_for(EventKind::Linked));
    }

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
        assert_b(events.wait_for(EventKind::SDPSet));
    }

    auto sdp = std::array<char, JUICE_MAX_SDP_STRING_LEN>();
    assert_b(juice_get_local_description(agent.get(), sdp.data(), sdp.size()) == JUICE_ERR_SUCCESS);
    PRINT(pad_name, " sdp: ", sdp.data());
    assert_b(wait_for_success(send_packet_relayed(proto::Type::SetCandidates, std::string_view(sdp.data()))));

    juice_gather_candidates(agent.get());
    assert_b(events.wait_for(EventKind::GatheringDone));
    assert_b(events.wait_for(EventKind::Connected));
    return true;
}

auto IceSession::stop() -> void {
    events.set(EventKind::Disconnected);
    websocket_context.shutdown();
    events.notifier.drain();
    if(signaling_worker.joinable()) {
        signaling_worker.join();
    }
}

auto IceSession::wait_for_success(const uint32_t packet_id) -> bool {
    unwrap_ob(result, events.wait_for(EventKind::Result, packet_id));
    return bool(result);
}

auto IceSession::send_payload(const std::span<const std::byte> payload) -> bool {
    assert_b(juice_send(agent.get(), (const char*)payload.data(), payload.size()) == 0);
    return true;
}

IceSession::~IceSession() {
    stop();
}
} // namespace p2p::ice
