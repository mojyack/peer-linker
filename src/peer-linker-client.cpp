#include "peer-linker-client.hpp"
#include "macros/coop-assert.hpp"
#include "macros/unwrap.hpp"
#include "net/tcp/client.hpp"
#include "peer-linker-protocol.hpp"
#include "protocol.hpp"

namespace plink {
auto PeerLinkerClientBackend::send(net::BytesRef data) -> coop::Async<bool> {
    return parser.send_packet(proto::Payload::pt, data.data(), data.size());
}

auto PeerLinkerClientBackend::finish() -> coop::Async<bool> {
    return inner.finish();
}

auto PeerLinkerClientBackend::connect(Params params) -> coop::Async<bool> {
    // setup inner backend
    inner.on_closed   = [this] { on_closed(); };
    inner.on_received = [this](net::BytesRef data) -> coop::Async<void> {
        if(const auto p = parser.parse_received(data)) {
            if(!co_await parser.callbacks.invoke(p->header, p->payload)) {
                const auto header = net::Header{
                    .type = proto::Error::pt,
                    .id   = p->header.id,
                    .size = 0,
                };
                co_await send({std::bit_cast<std::byte*>(&header), sizeof(header)});
            }
        }
    };

    auto linked = coop::SingleEvent();

    // setup parser
    // bind parser to backend
    parser.send_data = [this](net::BytesRef data) { return inner.send(data); };
    // packet type callbacks
    parser.callbacks.by_type[proto::Unlinked::pt] = [this](net::Header /*header*/, net::BytesRef /*payload*/) -> coop::Async<bool> {
        on_closed();
        co_return true;
    };
    parser.callbacks.by_type[proto::Auth::pt] = [this, &linked](net::Header header, net::BytesRef payload) -> coop::Async<bool> {
        constexpr auto error_value = false;
        co_unwrap_v(request, (serde::load<net::BinaryFormat, proto::Auth>(payload)));
        const auto ok = on_auth_request(request.requester_name, request.secret);
        co_ensure_v(co_await parser.send_packet(proto::AuthResponse{request.requester_name, ok}, header.id));
        linked.notify();

        parser.callbacks.by_type.erase(proto::Auth::pt); // don't need anymore
        co_return true;
    };
    parser.callbacks.by_type[proto::Payload::pt] = [this](net::Header /*header*/, const net::BytesRef payload) -> coop::Async<bool> {
        co_await on_received(payload);
        co_return true;
    };

    // start inner backend
    coop_ensure(co_await inner.connect(new net::tcp::TCPClientBackend(), params.peer_linker_addr, params.peer_linker_port));

    // start negotiation
    coop_ensure(co_await parser.receive_response<proto::Success>(proto::ActivateSession{params.user_certificate}));
    coop_ensure(co_await parser.receive_response<proto::Success>(proto::RegisterPad{params.pad_name}));
    on_pad_created();
    if(params.peer_info) {
        coop_ensure(co_await parser.receive_response<proto::Success>(proto::Link{params.peer_info->pad_name, params.peer_info->secret}));
    } else {
        co_await linked; // i.e. send auth response
    }
    co_return true;
}
} // namespace plink
