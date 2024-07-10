#include "macros/unwrap.hpp"
#include "p2p/channel-hub-client.hpp"
#include "util/assert.hpp"

namespace {
class ChannelHubSender : public p2p::chub::ChannelHubSender {
  private:
    int pad_id = 0;

    auto on_pad_request(const uint16_t request_id, const std::string_view channel_name) -> void override {
        notify_pad_created(request_id, build_string(channel_name, "_pad-", pad_id += 1));
    }

  public:
};

auto run() -> bool {
    const auto channel_hub = p2p::wss::ServerLocation{"localhost", 8081};

    auto sender   = ChannelHubSender();
    auto receiver = p2p::chub::ChannelHubReceiver();
    assert_b(sender.start(channel_hub));
    assert_b(receiver.start(channel_hub));

    assert_b(sender.register_channel("room-1-audio"));
    assert_b(sender.register_channel("room-1-video"));
    assert_b(sender.register_channel("room-1-audiovideo"));
    for(const auto& channel : receiver.get_channels()) {
        print("channel: ", channel);
    }
    unwrap_ob(pad_name, receiver.request_pad("room-1-audio"));
    print("acquired pad: ", pad_name);
    return true;
}
} // namespace

auto main() -> int {
    return run() ? 0 : 1;
}
