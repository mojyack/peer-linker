#include "protocol-helper.hpp"
#include "server-args.hpp"
#include "ws/server.hpp"

struct Session {
    virtual auto handle_payload(std::span<const std::byte> payload) -> bool = 0;
    virtual ~Session() {}
};

struct Server {
    ws::server::Context websocket_context;
    bool                verbose = false;

    template <class... Args>
    auto send_to(lws* const wsi, const uint16_t type, const uint32_t id, Args... args) -> bool {
        return websocket_context.send(wsi, p2p::proto::build_packet(type, id, args...));
    }
};

auto run(const ServerArgs& args, Server& server, std::unique_ptr<ws::server::SessionDataInitializer> session_initer, const char* protocol, const uint16_t error_type) -> bool;
