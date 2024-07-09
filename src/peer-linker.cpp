#include "macros/unwrap.hpp"
#include "peer-linker-protocol.hpp"
#include "protocol-helper.hpp"
#include "util/assert.hpp"
#include "util/string-map.hpp"
#include "ws/misc.hpp"
#include "ws/server.hpp"

namespace p2p {
namespace {
struct Pad {
    std::string name;
    std::string authenticator_name;
    lws*        wsi    = nullptr;
    Pad*        linked = nullptr;
};

struct Error {
    enum {
        EmptyPadName = 0,
        AlreadyRegistered,
        NotRegistered,
        PadFound,
        PadNotFound,
        AlreadyLinked,
        NotLinked,
        AuthInProgress,
        AuthNotInProgress,
        AutherMismatched,

        Limit,
    };
};

const auto estr = std::array{
    "empty pad name",                        // EmptyPadName
    "session already has pad",               // AlreadyRegistered
    "session has no pad",                    // NotRegistered
    "pad with that name already registered", // PadFound
    "no such pad registered",                // PadNotFound
    "pad already linked",                    // AlreadyLinked
    "pad not linked",                        // NotLinked
    "another authentication in progress",    // AuthInProgress
    "pad not authenticating",                // AuthNotInProgress
    "authenticator mismatched",              // AutherMismatched
};

static_assert(Error::Limit == estr.size());

struct Server {
    StringMap<Pad> pads;

    auto remove_pad(Pad* pad) -> void {
        if(pad == nullptr) {
            return;
        }
        if(pad->linked) {
            proto::send_packet(pad->linked->wsi, plink::proto::Type::Unlinked, 0);
            pad->linked->linked = nullptr;
        }
        pads.erase(pad->name);
    }
};

struct Session {
    Server* server;
    lws*    wsi;
    Pad*    pad = nullptr;

    auto handle_payload(std::span<const std::byte> payload) -> bool;
};

auto Session::handle_payload(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, proto::extract_header(payload));
    switch(header.type) {
    case plink::proto::Type::Register: {
        const auto name = proto::extract_last_string<plink::proto::Register>(payload);
        PRINT("received pad register request name:", name);

        assert_b(!name.empty(), estr[Error::EmptyPadName]);
        assert_b(pad == nullptr, estr[Error::AlreadyRegistered]);
        assert_b(server->pads.find(name) == server->pads.end(), estr[Error::PadFound]);

        PRINT("pad ", name, " registerd");
        pad = &server->pads.insert(std::pair{name, Pad{std::string(name), "", wsi, nullptr}}).first->second;
    } break;
    case plink::proto::Type::Unregister: {
        PRINT("received unregister request");

        assert_b(pad != nullptr, estr[Error::NotRegistered]);

        PRINT("unregistering pad ", pad->name);
        server->remove_pad(pad);
        pad = nullptr;
    } break;
    case plink::proto::Type::Link: {
        const auto requestee_name = proto::extract_last_string<plink::proto::Link>(payload);
        PRINT("received pad link request to ", requestee_name);

        assert_b(pad != nullptr, estr[Error::NotRegistered]);
        assert_b(pad->linked == nullptr, estr[Error::AlreadyLinked]);
        assert_b(pad->authenticator_name.empty(), estr[Error::AuthInProgress]);
        const auto it = server->pads.find(requestee_name);
        assert_b(it != server->pads.end(), estr[Error::PadNotFound]);
        auto& requestee = it->second;

        PRINT("sending auth request from ", pad->name, " to ", requestee_name);
        pad->authenticator_name = requestee.name;
        proto::send_packet(requestee.wsi, plink::proto::Type::LinkAuth, 0, pad->name);
    } break;
    case plink::proto::Type::Unlink: {
        PRINT("received unlink request");

        assert_b(pad != nullptr, estr[Error::NotRegistered]);
        assert_b(pad->linked != nullptr, estr[Error::NotLinked]);

        PRINT("unlinking pad ", pad->name, " and ", pad->linked->name);
        proto::send_packet(pad->linked->wsi, plink::proto::Type::Unlinked, 0);
        pad->linked->linked = nullptr;
        pad->linked         = nullptr;
    } break;
    case plink::proto::Type::LinkAuthResponse: {
        unwrap_pb(packet, proto::extract_payload<plink::proto::LinkAuthResponse>(payload));
        const auto requester_name = proto::extract_last_string<plink::proto::LinkAuthResponse>(payload);
        PRINT("received link auth to name: ", requester_name, " ok: ", int(packet.ok));

        assert_b(pad != nullptr, estr[Error::NotRegistered]);

        const auto it = server->pads.find(requester_name);
        assert_b(it != server->pads.end(), estr[Error::PadNotFound]);
        auto& requester = it->second;
        assert_b(!requester.authenticator_name.empty(), estr[Error::AuthNotInProgress]);
        assert_b(pad->name == requester.authenticator_name, estr[Error::AutherMismatched]);

        pad->authenticator_name.clear();
        if(packet.ok == 0) {
            proto::send_packet(requester.wsi, plink::proto::Type::LinkDenied, header.id);
        } else {
            PRINT("linking ", pad->name, " and ", requester.name);
            pad->linked      = &requester;
            requester.linked = pad;
            proto::send_packet(requester.wsi, plink::proto::Type::LinkSuccess, 0);
        }
    } break;
    default: {
        PRINT("received general command ", int(header.type));

        assert_b(pad != nullptr, estr[Error::NotRegistered]);
        assert_b(pad->linked != nullptr, estr[Error::NotLinked]);

        PRINT("passthroughing packet from ", pad->name, " to ", pad->linked->name);
        ws::write_back(pad->linked->wsi, payload.data(), payload.size());
        return true;
    }
    }

    proto::send_packet(wsi, plink::proto::Type::Success, header.id);
    return true;
}

struct SessionDataInitializer : ws::server::SessionDataInitializer {
    Server* server;

    auto get_size() -> size_t override {
        return sizeof(Session);
    }

    auto init(void* const ptr, lws* wsi) -> void override {
        PRINT("session created: ", ptr);
        auto& session  = *new(ptr) Session();
        session.server = server;
        session.wsi    = wsi;
    }

    auto deinit(void* const ptr) -> void override {
        PRINT("session destroyed: ", ptr);
        auto& session = *std::bit_cast<Session*>(ptr);
        server->remove_pad(session.pad);
        session.~Session();
    }

    SessionDataInitializer(Server& server)
        : server(&server) {}
};

auto run() -> bool {
    auto server = Server();

    auto wsctx    = ws::server::Context();
    wsctx.handler = [](lws* wsi, std::span<const std::byte> payload) -> void {
        auto& session = *std::bit_cast<Session*>(ws::server::wsi_to_userdata(wsi));
        PRINT("session ", &session, ": ", "received ", payload.size(), " bytes");
        if(!session.handle_payload(payload)) {
            WARN("payload handling failed");

            const auto& header_o = proto::extract_header(payload);
            if(!header_o) {
                WARN("packet too short");
                proto::send_packet(wsi, plink::proto::Type::Error, 0);
            } else {
                proto::send_packet(wsi, plink::proto::Type::Error, header_o->id);
            }
        }
    };
    wsctx.session_data_initer.reset(new SessionDataInitializer(server));
    wsctx.verbose      = true;
    wsctx.dump_packets = true;
    assert_b(wsctx.init(8080, "message"));
    while(wsctx.state == ws::server::State::Connected) {
        wsctx.process();
    }
    return true;
}
} // namespace
} // namespace p2p

auto main() -> int {
    return p2p::run() ? 0 : 1;
}
