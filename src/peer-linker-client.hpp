#pragma once
#include <coop/generator.hpp>
#include <coop/runner-pre.hpp>

#include "net/backend.hpp"
#include "net/enc/client.hpp"
#include "net/packet-parser.hpp"

namespace plink {
struct PeerLinkerClientBackend : net::ClientBackend {
    // private
    net::enc::ClientBackendEncAdaptor inner;
    net::PacketParser                 parser;

    // overrides
    auto send(net::BytesRef data) -> coop::Async<bool> override;
    auto finish() -> coop::Async<bool> override;

    // backend-specific
    std::function<void()>                                            on_pad_created  = [] {};
    std::function<bool(std::string_view name, net::BytesRef secret)> on_auth_request = [](std::string_view, net::BytesRef) { return false; };

    struct Params {
        struct PeerInfo {
            std::string     pad_name;
            net::BytesArray secret;
        };

        const char*             peer_linker_addr;
        uint16_t                peer_linker_port;
        std::string             pad_name;
        std::optional<PeerInfo> peer_info        = {};
        std::string             user_certificate = {};
    };
    auto connect(Params params) -> coop::Async<bool>;
};
} // namespace plink
