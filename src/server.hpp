#include "protocol-helper.hpp"
#include "ws/server.hpp"

struct Server {
    ws::server::Context websocket_context;
    bool                verbose = false;

    template <class... Args>
    auto send_to(lws* const wsi, const uint16_t type, const uint32_t id, Args... args) -> bool {
        return websocket_context.send(wsi, p2p::proto::build_packet(type, id, args...));
    }
};
