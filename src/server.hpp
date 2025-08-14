#pragma once
#include <coop/mutex.hpp>

#include "net/backend.hpp"
#include "net/packet-parser.hpp"
#include "session-key.hpp"
#include "util/logger-pre.hpp"

namespace plink {
struct Server;

struct Session {
    net::PacketParser parser;
    bool              activated = false;

    auto         handle_activation(net::BytesRef payload, Server& server) -> bool;
    virtual auto on_received(PrependableBuffer buffer) -> coop::Async<bool> = 0;

    virtual ~Session() {}
};

struct Server {
    std::unique_ptr<net::ServerBackend> backend;
    std::optional<SessionKey>           session_key;
    std::string                         user_cert_verifier;
    coop::Mutex                         mutex;
    Logger                              logger;

    virtual auto alloc_session() -> coop::Async<Session*>        = 0;
    virtual auto free_session(Session* ptr) -> coop::Async<void> = 0;
    virtual ~Server() {};
};

auto run(int argc, const char* const* argv, uint16_t port, Server& server, std::string_view name) -> bool;
} // namespace plink
