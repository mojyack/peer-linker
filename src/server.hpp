#pragma once
#include "protocol-helper.hpp"
#include "session-key.hpp"
#include "ws/server.hpp"

struct Server {
    ws::server::Context       websocket_context;
    std::optional<SessionKey> session_key;
    std::string               user_cert_verifier;
    bool                      verbose = false;

    template <class... Args>
    auto send_to(ws::server::Client* const client, const uint16_t type, const uint32_t id, Args... args) -> bool {
        return websocket_context.send(client, p2p::proto::build_packet(type, id, args...));
    }
};

struct Session {
    bool activated = false;

    virtual auto handle_payload(std::span<const std::byte> payload) -> bool = 0;

    auto activate(Server& server, std::string_view cert) -> bool;

    virtual ~Session() {}
};

auto run(int argc, const char* const* argv,
         uint16_t                                            default_port,
         Server&                                             server,
         std::unique_ptr<ws::server::SessionDataInitializer> session_initer,
         const char*                                         protocol) -> bool;
