#include <coop/runner.hpp>
#include <coop/task-handle.hpp>
#include <coop/timer.hpp>

#include "macros/assert.hpp"
#include "plink/peer-linker-client.hpp"
#include "util/concat.hpp"
#include "util/span.hpp"

using Client = plink::PeerLinkerClientBackend;

auto prepare_client_1(Client& client) -> void {
    client.on_auth_request = [](std::string_view name, net::BytesRef secret) -> bool {
        constexpr auto error_value = false;
        ensure_v(name == "2");
        ensure_v(from_span(secret) == "SECRET");
        return true;
    };
}

auto prepare_client_2(Client&) -> void {
}

auto start_client_1(Client& client) -> coop::Async<bool> {
    co_return co_await client.connect({
        .peer_linker_addr = "localhost",
        .peer_linker_port = 8080,
        .pad_name         = "1",
    });
}

auto start_client_2(Client& client) -> coop::Async<bool> {
    co_return co_await client.connect({
        .peer_linker_addr = "localhost",
        .peer_linker_port = 8080,
        .pad_name         = "2",
        .peer_info        = plink::PeerLinkerClientBackend::Params::PeerInfo{
                   .pad_name = "1",
                   .secret   = copy(to_span("SECRET")),
        },
    });
}

#include "common.cpp"

auto main() -> int {
    auto runner = coop::Runner();
    runner.push_task(test());
    runner.run();

    if(pass) {
        std::println("pass");
        return 0;
    } else {
        return -1;
    }
}
