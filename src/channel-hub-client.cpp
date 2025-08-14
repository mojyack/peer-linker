#include "channel-hub-client.hpp"
#include "channel-hub-protocol.hpp"
#include "macros/coop-unwrap.hpp"
#include "net/tcp/client.hpp"
#include "protocol.hpp"

namespace plink {
auto ChannelHubClient::register_channel(std::string channel) -> coop::Async<bool> {
    coop_ensure(co_await parser.receive_response<proto::Success>(proto::RegisterChannel{std::move(channel)}));
    co_return true;
}

auto ChannelHubClient::unregister_channel(std::string channel) -> coop::Async<bool> {
    coop_ensure(co_await parser.receive_response<proto::Success>(proto::UnregisterChannel{std::move(channel)}));
    co_return true;
}

auto ChannelHubClient::get_channels() -> coop::Async<std::optional<std::vector<std::string>>> {
    coop_unwrap_mut(channels, co_await parser.receive_response<proto::Channels>(proto::GetChannels()));
    co_return std::move(channels.channels);
}

auto ChannelHubClient::request_pad(std::string channel) -> coop::Async<std::optional<std::string>> {
    coop_unwrap_mut(resp, co_await parser.receive_response<proto::PadCreated>(proto::RequestPad{std::move(channel)}));
    coop_ensure(!resp.pad_name.empty());
    co_return std::move(resp.pad_name);
}

auto ChannelHubClient::connect(const char* const addr, const uint16_t port, std::string user_certificate) -> coop::Async<bool> {
    // this->backend.reset(backend);
    backend.on_closed   = [this] { on_closed(); };
    backend.on_received = [this](PrependableBuffer buffer) -> coop::Async<void> {
        co_await parser.callbacks.invoke(std::move(buffer));
    };
    parser.send_data                                = [this](PrependableBuffer buffer) { return backend.send(std::move(buffer)); };
    parser.callbacks.by_type[proto::RequestPad::pt] = [this](const net::Header header, PrependableBuffer buffer) -> coop::Async<bool> {
        constexpr auto error_value = false;
        co_unwrap_v_mut(request, (serde::load<net::BinaryFormat, proto::RequestPad>(buffer.body())));
        auto pad_name = co_await on_pad_request(request.channel_name);
        auto result   = proto::PadCreated{std::move(request.channel_name)};
        if(pad_name) {
            result.pad_name = std::move(*pad_name);
        }
        co_ensure_v(co_await parser.send_packet(std::move(result), header.id));
        co_return true;
    };

    coop_ensure(co_await backend.connect(new net::tcp::TCPClientBackend(), addr, port));
    coop_ensure(co_await parser.receive_response<proto::Success>(proto::ActivateSession{std::move(user_certificate)}));
    co_return true;
}
} // namespace plink
