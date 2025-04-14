#pragma once
#include <optional>

#include "net/enc/client.hpp"
#include "net/packet-parser.hpp"

namespace plink {
struct ChannelHubClient {
    // private
    net::enc::ClientBackendEncAdaptor backend;
    net::PacketParser                 parser;

    // callbacks
    std::function<coop::Async<std::optional<std::string>>(std::string_view channel)> on_pad_request = [](std::string_view) -> coop::Async<std::optional<std::string>> { co_return std::nullopt; };
    std::function<void()>                                                            on_closed      = [] {};

    auto register_channel(std::string channel) -> coop::Async<bool>;
    auto unregister_channel(std::string channel) -> coop::Async<bool>;
    auto get_channels() -> coop::Async<std::optional<std::vector<std::string>>>;
    auto request_pad(std::string channel) -> coop::Async<std::optional<std::string>>;

    auto connect(const char* addr, uint16_t port, std::string user_certificate = {}) -> coop::Async<bool>;
};
} // namespace plink
