#include "macros/unwrap.hpp"
#include "p2p/channel-hub-client.hpp"
#include "util/argument-parser.hpp"
#include "util/file-io.hpp"
#include "util/span.hpp"

namespace {
class ChannelHubSender : public p2p::chub::ChannelHubSender {
  private:
    int pad_id = 0;

    auto on_pad_request(const uint16_t request_id, const std::string_view channel_name) -> bool override {
        notify_pad_created(request_id, build_string(channel_name, "_pad-", pad_id += 1));
        return true;
    }

  public:
};

auto run(const int argc, const char* const* const argv) -> bool {
    auto cert_file         = (const char*)(nullptr);
    auto allow_self_signed = false;
    auto help              = false;

    auto parser = args::Parser<uint16_t, uint8_t>();
    parser.kwarg(&help, {"-h", "--help"}, {.arg_desc = "print this help message", .state = args::State::Initialized, .no_error_check = true});
    parser.kwarg(&cert_file, {"-k"}, {"CERT_FILE", "use user certificate", args::State::Initialized});
    parser.kwarg(&allow_self_signed, {"-a"}, {"", "allow self signed ssl certificate", args::State::Initialized});
    if(!parser.parse(argc, argv) || help) {
        print("usage: channel-hub-client-test ", parser.get_help());
        return true;
    }

    auto user_cert = std::string();
    if(cert_file != nullptr) {
        unwrap_ob(cert, read_file(cert_file));
        user_cert = from_span(cert);
    }

    const auto channel_hub = p2p::wss::ServerLocation{"localhost", 8081};

    auto sender    = ChannelHubSender();
    sender.verbose = true;
    sender.set_ws_debug_flags(true, true);
    auto receiver = p2p::chub::ChannelHubReceiver();
    assert_b(sender.start({channel_hub, user_cert, allow_self_signed}));
    assert_b(receiver.start({channel_hub, user_cert, allow_self_signed}));

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

auto main(const int argc, const char* const* const argv) -> int {
    return run(argc, argv) ? 0 : 1;
}
