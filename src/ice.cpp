#include "ice.hpp"
#include "macros/unwrap.hpp"
#include "signaling-protocol-helper.hpp"
#include "util/assert.hpp"

namespace p2p::ice {
namespace {
auto set_result(IceSession& session, const bool result) -> void {
    session.result = result;
    session.result_event.wakeup();
}

auto set_connected(IceSession& session, const bool connected) -> void {
    session.connected = connected;
    set_result(session, connected);
}

auto on_state_changed(juice_agent_t* const /*agent*/, const juice_state_t state, void* const user_ptr) -> void {
    PRINT("state changed: ", juice_state_to_string(state));
    auto& session = *std::bit_cast<IceSession*>(user_ptr);
    switch(state) {
    case JUICE_STATE_COMPLETED:
        set_connected(session, true);
        break;
    case JUICE_STATE_FAILED:
        set_connected(session, false);
        break;
    default:
        break;
    }
}

auto on_candidate(juice_agent_t* const /*agent*/, const char* const sdp, void* const user_ptr) -> void {
    PRINT("new candidate: ", sdp);
    auto& session = *std::bit_cast<IceSession*>(user_ptr);
    session.gathering_done_event.wakeup();
    proto::send_packet(session.websocket_context.wsi, proto::Type::AddCandidates, std::string_view(sdp));
}

auto on_gathering_done(juice_agent_t* const /*agent*/, void* const user_ptr) -> void {
    PRINT("gathering done");
    const auto wsi = std::bit_cast<IceSession*>(user_ptr)->websocket_context.wsi;
    proto::send_packet(wsi, proto::Type::GatheringDone);
}

auto on_recv(juice_agent_t* const /*agent*/, const char* const data, const size_t size, void* const user_ptr) -> void {
    std::bit_cast<IceSession*>(user_ptr)->on_p2p_data({(std::byte*)data, size});
}
} // namespace

auto IceSession::on_p2p_data(const std::span<const std::byte> payload) -> void {
    PRINT("p2p data received: ", payload.size(), " bytes");
}

auto IceSession::handle_payload(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));

    switch(header.type) {
    case proto::Type::Success:
        set_result(*this, true);
        return true;
    case proto::Type::Error:
        set_result(*this, false);
        return true;
    case proto::Type::SetCandidates: {
        const auto sdp = proto::extract_last_string<proto::SetCandidates>(payload);
        PRINT("received remote candidates: ", sdp);
        juice_set_remote_description(agent.get(), sdp.data());
        sdp_set_event.wakeup();

        proto::send_packet(websocket_context.wsi, proto::Type::Success);
        return true;
    }
    case proto::Type::AddCandidates: {
        const auto sdp = proto::extract_last_string<proto::AddCandidates>(payload);
        PRINT("received additional candidates: ", sdp);
        juice_add_remote_candidate(agent.get(), sdp.data());

        proto::send_packet(websocket_context.wsi, proto::Type::Success);
        return true;
    }
    case proto::Type::GatheringDone: {
        PRINT("received gathering done");
        juice_set_remote_gathering_done(agent.get());

        proto::send_packet(websocket_context.wsi, proto::Type::Success);
        return true;
    }
    default:
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto IceSession::start(const char* const server, const uint16_t port, const std::string_view pad_name, const std::string_view target_pad_name, const char* turn_server, uint16_t turn_port) -> bool {
    websocket_context.handler = [this](std::span<const std::byte> payload) -> void {
        PRINT("received ", payload.size(), " bytes");
        if(!handle_payload(payload)) {
            WARN("payload handling failed");
            send_packet(websocket_context.wsi, proto::Type::Error);
        }
    };
    websocket_context.dump_packets = true;
    assert_b(websocket_context.init(server, port, "/", "message", ws::client::SSLLevel::NoSSL));
    signaling_worker = std::thread([this]() -> void {
        while(websocket_context.state == ws::client::State::Connected) {
            websocket_context.process();
        }
        set_connected(*this, false);
    });

    const auto controlled = target_pad_name.empty();

    proto::send_packet(websocket_context.wsi, proto::Type::Register, pad_name);
    assert_b(wait_for_success());
    if(!controlled) {
        proto::send_packet(websocket_context.wsi, proto::Type::Link, target_pad_name);
        assert_b(wait_for_success());
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
        sdp_set_event.wait();
    }

    auto sdp = std::array<char, JUICE_MAX_SDP_STRING_LEN>();
    assert_b(juice_get_local_description(agent.get(), sdp.data(), sdp.size()) == JUICE_ERR_SUCCESS);
    PRINT(pad_name, " sdp: ", sdp.data())
    proto::send_packet(websocket_context.wsi, proto::Type::SetCandidates, std::string_view(sdp.data()));
    assert_b(wait_for_success());

    juice_gather_candidates(agent.get());
    gathering_done_event.wait();

    while(!connected) {
        assert_b(wait_for_success());
    }

    return true;
}

auto IceSession::stop() -> void {
    if(!connected) {
        return;
    }
    set_connected(*this, false);
    websocket_context.shutdown();
    signaling_worker.join();
}

auto IceSession::wait_for_success() -> bool {
    result_event.wait();
    result_event.clear();
    return result;
}

auto IceSession::send_payload(const std::span<const std::byte> payload) -> bool {
    assert_b(connected);
    assert_b(juice_send(agent.get(), (const char*)payload.data(), payload.size()) == 0);
    return true;
}

auto IceSession::send_payload_relayed(const std::span<const std::byte> payload) -> bool {
    assert_b(connected);
    ws::write_back(websocket_context.wsi, payload.data(), payload.size());
    return true;
}

IceSession::~IceSession() {
    stop();
}
} // namespace p2p::ice
