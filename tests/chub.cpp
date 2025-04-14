#include <coop/generator.hpp>
#include <coop/promise.hpp>
#include <coop/runner.hpp>

#include "macros/coop-unwrap.hpp"
#include "plink/channel-hub-client.hpp"

namespace {
auto reg_unreg_test() -> coop::Async<bool> {
    auto c1 = plink::ChannelHubClient();
    coop_ensure(co_await c1.connect("localhost", 8081));
    coop_ensure(co_await c1.register_channel("channel1"));
    coop_ensure(co_await c1.register_channel("channel2"));
    coop_ensure(co_await c1.register_channel("channel3"));
    {
        coop_unwrap(channels, co_await c1.get_channels());
        coop_ensure(channels.size() == 3);
        coop_ensure(channels[0] == "channel1");
        coop_ensure(channels[1] == "channel2");
        coop_ensure(channels[2] == "channel3");
    }
    coop_ensure(co_await c1.unregister_channel("channel1"));
    coop_ensure(co_await c1.unregister_channel("channel3"));
    {
        coop_unwrap(channels, co_await c1.get_channels());
        coop_ensure(channels.size() == 1);
        coop_ensure(channels[0] == "channel2");
    }
    coop_ensure(!co_await c1.register_channel("channel2"));
    co_return true;
}

auto pad_request_test() -> coop::Async<bool> {
    struct Local {
        plink::ChannelHubClient c1;
        plink::ChannelHubClient c2;
        int                     a_pads;
        int                     b_pads;

        auto on_pad_request(std::string_view channel) -> coop::Async<std::optional<std::string>> {
            if(channel == "a" && a_pads < 2) {
                a_pads += 1;
                co_return std::format("pad_a_{}", a_pads);
            } else if(channel == "b" && b_pads < 2) {
                b_pads += 1;
                co_return std::format("pad_b_{}", b_pads);
            } else {
                co_return std::nullopt;
            }
        }
    };
    auto local = Local();

    local.c1.on_pad_request = [&local](std::string_view channel) { return local.on_pad_request(channel); };
    coop_ensure(co_await local.c1.connect("localhost", 8081));
    coop_ensure(co_await local.c2.connect("localhost", 8081));

    coop_ensure(co_await local.c1.register_channel("a"));
    coop_ensure(co_await local.c1.register_channel("b"));

    coop_ensure(co_await local.c2.request_pad("a") == "pad_a_1");
    coop_ensure(co_await local.c2.request_pad("b") == "pad_b_1");
    coop_ensure(co_await local.c2.request_pad("a") == "pad_a_2");
    coop_ensure(co_await local.c2.request_pad("b") == "pad_b_2");
    coop_ensure(!co_await local.c2.request_pad("a"));
    coop_ensure(!co_await local.c2.request_pad("b"));
    coop_ensure(!co_await local.c2.request_pad("c"));

    co_return true;
}

auto pass = false;

auto run_tests() -> coop::Async<void> {
    coop_ensure(co_await reg_unreg_test());
    coop_ensure(co_await pad_request_test());
    pass = true;
}
} // namespace

auto main() -> int {
    auto runner = coop::Runner();
    runner.push_task(run_tests());
    runner.run();

    if(pass) {
        std::println("pass");
        return 0;
    } else {
        return -1;
    }
}
