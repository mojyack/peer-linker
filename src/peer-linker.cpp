#include "macros/unwrap.hpp"
#include "peer-linker-protocol.hpp"
#include "server-args.hpp"
#include "server.hpp"
#include "util/string-map.hpp"

namespace p2p::plink {
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

struct PeerLinker : Server {
    StringMap<Pad> pads;

    auto remove_pad(Pad* pad) -> void {
        if(pad == nullptr) {
            return;
        }
        if(pad->linked) {
            send_to(pad->linked->wsi, proto::Type::Unlinked, 0);
            pad->linked->linked = nullptr;
        }
        pads.erase(pad->name);
    }
};

struct PeerLinkerSession : Session {
    PeerLinker* server;
    lws*        wsi;
    Pad*        pad = nullptr;

    auto handle_payload(std::span<const std::byte> payload) -> bool override;
};

auto PeerLinkerSession::handle_payload(const std::span<const std::byte> payload) -> bool {
    unwrap_pb(header, p2p::proto::extract_header(payload));
    switch(header.type) {
    case proto::Type::Register: {
        const auto name = p2p::proto::extract_last_string<proto::Register>(payload);
        PRINT("received pad register request name:", name);

        assert_b(!name.empty(), estr[Error::EmptyPadName]);
        assert_b(pad == nullptr, estr[Error::AlreadyRegistered]);
        assert_b(server->pads.find(name) == server->pads.end(), estr[Error::PadFound]);

        PRINT("pad ", name, " registerd");
        pad = &server->pads.insert(std::pair{name, Pad{std::string(name), "", wsi, nullptr}}).first->second;
    } break;
    case proto::Type::Unregister: {
        PRINT("received unregister request");

        assert_b(pad != nullptr, estr[Error::NotRegistered]);

        PRINT("unregistering pad ", pad->name);
        server->remove_pad(pad);
        pad = nullptr;
    } break;
    case proto::Type::Link: {
        unwrap_pb(packet, p2p::proto::extract_payload<proto::Link>(payload));
        assert_b(sizeof(proto::Link) + packet.requestee_name_len + packet.secret_len == payload.size());
        const auto requestee_name = std::string_view(std::bit_cast<char*>(payload.data() + sizeof(proto::Link)), packet.requestee_name_len);
        const auto secret         = std::span(payload.data() + sizeof(proto::Link) + packet.requestee_name_len, packet.secret_len);
        PRINT("received pad link request to ", requestee_name);

        assert_b(pad != nullptr, estr[Error::NotRegistered]);
        assert_b(pad->linked == nullptr, estr[Error::AlreadyLinked]);
        assert_b(pad->authenticator_name.empty(), estr[Error::AuthInProgress]);
        const auto it = server->pads.find(requestee_name);
        assert_b(it != server->pads.end(), estr[Error::PadNotFound]);
        auto& requestee = it->second;

        PRINT("sending auth request from ", pad->name, " to ", requestee_name);
        assert_b(server->send_to(requestee.wsi, proto::Type::LinkAuth, 0,
                                 uint16_t(pad->name.size()),
                                 uint16_t(secret.size()),
                                 pad->name,
                                 secret));
        pad->authenticator_name = requestee.name;
    } break;
    case proto::Type::Unlink: {
        PRINT("received unlink request");

        assert_b(pad != nullptr, estr[Error::NotRegistered]);
        assert_b(pad->linked != nullptr, estr[Error::NotLinked]);

        PRINT("unlinking pad ", pad->name, " and ", pad->linked->name);
        assert_b(server->send_to(pad->linked->wsi, proto::Type::Unlinked, 0));
        pad->linked->linked = nullptr;
        pad->linked         = nullptr;
    } break;
    case proto::Type::LinkAuthResponse: {
        unwrap_pb(packet, p2p::proto::extract_payload<proto::LinkAuthResponse>(payload));
        const auto requester_name = p2p::proto::extract_last_string<proto::LinkAuthResponse>(payload);
        PRINT("received link auth to name: ", requester_name, " ok: ", int(packet.ok));

        assert_b(pad != nullptr, estr[Error::NotRegistered]);

        const auto it = server->pads.find(requester_name);
        assert_b(it != server->pads.end(), estr[Error::PadNotFound]);
        auto& requester = it->second;
        assert_b(!requester.authenticator_name.empty(), estr[Error::AuthNotInProgress]);
        assert_b(pad->name == requester.authenticator_name, estr[Error::AutherMismatched]);

        pad->authenticator_name.clear();
        if(packet.ok == 0) {
            assert_b(server->send_to(requester.wsi, proto::Type::LinkDenied, header.id));
        } else {
            PRINT("linking ", pad->name, " and ", requester.name);
            assert_b(server->send_to(requester.wsi, proto::Type::LinkSuccess, 0));
            pad->linked      = &requester;
            requester.linked = pad;
        }
    } break;
    default: {
        if(server->verbose) {
            PRINT("received general command ", int(header.type));
        }

        assert_b(pad != nullptr, estr[Error::NotRegistered]);
        assert_b(pad->linked != nullptr, estr[Error::NotLinked]);

        if(server->verbose) {
            PRINT("passthroughing packet from ", pad->name, " to ", pad->linked->name);
        }

        assert_b(server->websocket_context.send(pad->linked->wsi, payload));
        return true;
    }
    }

    assert_b(server->send_to(wsi, proto::Type::Success, header.id));
    return true;
}

struct SessionDataInitializer : ws::server::SessionDataInitializer {
    PeerLinker* server;

    auto get_size() -> size_t override {
        return sizeof(PeerLinkerSession);
    }

    auto init(void* const ptr, lws* wsi) -> void override {
        PRINT("session created: ", ptr);
        auto& session  = *new(ptr) PeerLinkerSession();
        session.server = server;
        session.wsi    = wsi;
    }

    auto deinit(void* const ptr) -> void override {
        PRINT("session destroyed: ", ptr);
        auto& session = *std::bit_cast<PeerLinkerSession*>(ptr);
        server->remove_pad(session.pad);
        session.~PeerLinkerSession();
    }

    SessionDataInitializer(PeerLinker& server)
        : server(&server) {}
};

auto run(const int argc, const char* argv[]) -> bool {
    unwrap_ob(args, ServerArgs::parse(argc, argv, "peer-linker", 8080));
    auto server = PeerLinker();
    auto initor = std::unique_ptr<ws::server::SessionDataInitializer>(new SessionDataInitializer(server));
    assert_b(run(args, server, std::move(initor), "peer-linker", proto::Type::Error));
    return true;
}
} // namespace
} // namespace p2p::plink

auto main(const int argc, const char* argv[]) -> int {
    return p2p::plink::run(argc, argv) ? 0 : 1;
}
