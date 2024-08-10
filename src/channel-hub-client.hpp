#pragma once
#include <optional>

#include "websocket-session.hpp"

namespace p2p::chub {
class ChannelHubSession : public wss::WebSocketSession {
  public:
    auto start(wss::ServerLocation channel_hub) -> bool;

    virtual ~ChannelHubSession();
};

class ChannelHubSender : public ChannelHubSession {
  private:
    auto on_packet_received(std::span<const std::byte> payload) -> bool override;

  public:
    virtual auto on_pad_request(uint16_t request_id, const std::string_view channel_name) -> bool = 0;

    auto register_channel(std::string_view name) -> bool;
    auto unregister_channel(std::string_view name) -> bool;
    auto notify_pad_created(uint16_t request_id, std::string_view pad_name) -> void;
    auto notify_pad_not_created(uint16_t request_id) -> void;
};

class ChannelHubReceiver : public ChannelHubSession {
  private:
    std::string channels_buffer;
    std::string pad_name_buffer;

    auto on_packet_received(std::span<const std::byte> payload) -> bool override;

  public:
    auto get_channels() -> std::vector<std::string>;
    auto request_pad(std::string_view channel_name) -> std::optional<std::string>;
};
} // namespace p2p::chub
