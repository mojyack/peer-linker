#include "channel-hub-client.hpp"
#include "channel-hub-protocol.hpp"
#include "macros/unwrap.hpp"
#include "protocol-helper.hpp"
#include "util/assert.hpp"

namespace p2p::chub {
struct EventKind {
    enum {
        Channels = wss::EventKind::Limit,
        PadCreated,

        Limit,
    };
};

// ChannelHubSession
auto ChannelHubSession::get_error_packet_type() const -> uint16_t {
    return chub::proto::Type::Error;
}

auto ChannelHubSession::start(const wss::ServerLocation channel_hub) -> bool {
    assert_b(wss::WebSocketSession::start(channel_hub, "channel-hub"));
    return true;
}

// ChannelHubSender
auto ChannelHubSender::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, ::p2p::proto::extract_header(payload));

    switch(header.type) {
    case chub::proto::Type::Success:
        events.invoke(wss::EventKind::Result, header.id, 1);
        return true;
    case chub::proto::Type::Error:
        events.invoke(wss::EventKind::Result, header.id, 0);
        return true;
    case chub::proto::Type::PadRequest: {
        const auto channel_name = ::p2p::proto::extract_last_string<chub::proto::PadRequest>(payload);
        assert_b(on_pad_request(header.id, channel_name));
        send_result(proto::Type::Success, header.id);
        return true;
    }
    default:
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto ChannelHubSender::register_channel(const std::string_view name) -> bool {
    assert_b(send_packet(chub::proto::Type::Register, name));
    return true;
}

auto ChannelHubSender::unregister_channel(const std::string_view name) -> bool {
    assert_b(send_packet(chub::proto::Type::Unregister, name));
    return true;
}

auto ChannelHubSender::notify_pad_created(const uint16_t request_id, const std::string_view pad_name) -> void {
    send_result(chub::proto::Type::PadRequestResponse, request_id, uint16_t(1), pad_name);
}

auto ChannelHubSender::notify_pad_not_created(const uint16_t request_id) -> void {
    send_result(chub::proto::Type::PadRequestResponse, request_id, uint16_t(0));
}

// ChannelHubReceiver
auto ChannelHubReceiver::on_packet_received(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, ::p2p::proto::extract_header(payload));

    switch(header.type) {
    case chub::proto::Type::Success:
        events.invoke(wss::EventKind::Result, header.id, 1);
        return true;
    case chub::proto::Type::Error:
        events.invoke(wss::EventKind::Result, header.id, 0);
        return true;
    case chub::proto::Type::GetChannelsResponse: {
        const auto channels = ::p2p::proto::extract_last_string<chub::proto::GetChannelsResponse>(payload);
        // TODO: use packet id
        if(!std::exchange(channels_buffer, channels).empty()) {
            WARN("previous get channels response not handled");
        }
        events.invoke(EventKind::Channels, header.id, no_value);
        return true;
    }
    case chub::proto::Type::PadRequestResponse: {
        pad_name_buffer = ::p2p::proto::extract_last_string<chub::proto::PadRequestResponse>(payload);
        events.invoke(EventKind::PadCreated, no_id, no_value);
        return true;
    }
    default:
        WARN("unhandled payload type ", int(header.type));
        return false;
    }
}

auto ChannelHubReceiver::get_channels() -> std::vector<std::string> {
    auto received = Event();

    const auto id = allocate_packet_id();
    events.add_handler({EventKind::Channels, id, [&](uint32_t) { received.notify(); }});
    send_generic_packet(chub::proto::Type::GetChannels, id);
    received.wait();

    const auto channels_str = std::exchange(channels_buffer, {});
    auto       channels     = std::vector<std::string>();

    // split string array
    auto head = size_t(0);
    auto tail = channels_str.find('\0');
    while(tail != channels_str.npos) {
        channels.emplace_back(channels_str.substr(head, tail - head));
        head = tail;
        tail = channels_str.find('\0', tail + 1);
    }

    return channels;
}

auto ChannelHubReceiver::request_pad(const std::string_view channel_name) -> std::optional<std::string> {
    // setup event handler before sending request
    auto received = Event();
    add_event_handler(EventKind::PadCreated, [&](uint32_t) { received.notify(); });

    // check for succeed of request
    assert_o(send_packet(chub::proto::Type::PadRequest, channel_name));

    // wait for response
    received.wait();

    return pad_name_buffer;
}
} // namespace p2p::chub
