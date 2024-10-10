#include "macros/unwrap.hpp"
#include "peer-linker-protocol.hpp"
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
        NotActivated = 0,
        EmptyPadName,
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
    "session is not activated",              // NotActivated
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
    unwrap(header, p2p::proto::extract_header(payload));

    if(header.type == ::p2p::proto::Type::ActivateSession) {
        const auto cert = p2p::proto::extract_last_string<proto::Register>(payload);
        print("received activate session");
        ensure(activate(*server, cert), "failed to verify user certificate");
        print("session activated");
        goto finish;
    } else {
        ensure(activated, estr[Error::NotActivated]);
    }

    switch(header.type) {
    case proto::Type::Register: {
        const auto name = p2p::proto::extract_last_string<proto::Register>(payload);
        print("received pad register request name: ", name);

        ensure(!name.empty(), estr[Error::EmptyPadName]);
        ensure(pad == nullptr, estr[Error::AlreadyRegistered]);
        ensure(server->pads.find(name) == server->pads.end(), estr[Error::PadFound]);

        print("pad ", name, " registerd");
        pad = &server->pads.insert(std::pair{name, Pad{std::string(name), "", wsi, nullptr}}).first->second;
    } break;
    case proto::Type::Unregister: {
        print("received unregister request");

        ensure(pad != nullptr, estr[Error::NotRegistered]);

        print("unregistering pad ", pad->name);
        server->remove_pad(pad);
        pad = nullptr;
    } break;
    case proto::Type::Link: {
        unwrap(packet, p2p::proto::extract_payload<proto::Link>(payload));
        ensure(sizeof(proto::Link) + packet.requestee_name_len + packet.secret_len == payload.size());
        const auto requestee_name = std::string_view(std::bit_cast<char*>(payload.data() + sizeof(proto::Link)), packet.requestee_name_len);
        const auto secret         = std::span(payload.data() + sizeof(proto::Link) + packet.requestee_name_len, packet.secret_len);
        print("received pad link request to ", requestee_name);

        ensure(pad != nullptr, estr[Error::NotRegistered]);
        ensure(pad->linked == nullptr, estr[Error::AlreadyLinked]);
        ensure(pad->authenticator_name.empty(), estr[Error::AuthInProgress]);
        const auto it = server->pads.find(requestee_name);
        ensure(it != server->pads.end(), estr[Error::PadNotFound]);
        auto& requestee = it->second;

        print("sending auth request from ", pad->name, " to ", requestee_name);
        ensure(server->send_to(requestee.wsi, proto::Type::LinkAuth, 0,
                               uint16_t(pad->name.size()),
                               uint16_t(secret.size()),
                               pad->name,
                               secret));
        pad->authenticator_name = requestee.name;
    } break;
    case proto::Type::Unlink: {
        print("received unlink request");

        ensure(pad != nullptr, estr[Error::NotRegistered]);
        ensure(pad->linked != nullptr, estr[Error::NotLinked]);

        print("unlinking pad ", pad->name, " and ", pad->linked->name);
        ensure(server->send_to(pad->linked->wsi, proto::Type::Unlinked, 0));
        pad->linked->linked = nullptr;
        pad->linked         = nullptr;
    } break;
    case proto::Type::LinkAuthResponse: {
        unwrap(packet, p2p::proto::extract_payload<proto::LinkAuthResponse>(payload));
        const auto requester_name = p2p::proto::extract_last_string<proto::LinkAuthResponse>(payload);
        print("received link auth to name: ", requester_name, " ok: ", int(packet.ok));

        ensure(pad != nullptr, estr[Error::NotRegistered]);

        const auto it = server->pads.find(requester_name);
        ensure(it != server->pads.end(), estr[Error::PadNotFound]);
        auto& requester = it->second;
        ensure(!requester.authenticator_name.empty(), estr[Error::AuthNotInProgress]);
        ensure(pad->name == requester.authenticator_name, estr[Error::AutherMismatched]);

        pad->authenticator_name.clear();
        if(packet.ok == 0) {
            ensure(server->send_to(requester.wsi, proto::Type::LinkDenied, header.id));
        } else {
            print("linking ", pad->name, " and ", requester.name);
            ensure(server->send_to(requester.wsi, proto::Type::LinkSuccess, 0));
            pad->linked      = &requester;
            requester.linked = pad;
        }
    } break;
    default: {
        if(server->verbose) {
            print("received general command ", int(header.type));
        }

        ensure(pad != nullptr, estr[Error::NotRegistered]);
        ensure(pad->linked != nullptr, estr[Error::NotLinked]);

        if(server->verbose) {
            print("passthroughing packet from ", pad->name, " to ", pad->linked->name);
        }

        ensure(server->websocket_context.send(pad->linked->wsi, payload));
        return true;
    }
    }

finish:
    ensure(server->send_to(wsi, ::p2p::proto::Type::Success, header.id));
    return true;
}

struct SessionDataInitializer : ws::server::SessionDataInitializer {
    PeerLinker* server;

    auto alloc(lws* const wsi) -> void* override {
        auto& session  = *(new PeerLinkerSession());
        session.server = server;
        session.wsi    = wsi;
        print("session created: ", &session);
        return &session;
    }

    auto free(void* ptr) -> void override {
        auto& session = *std::bit_cast<PeerLinkerSession*>(ptr);
        server->remove_pad(session.pad);
        delete &session;
        print("session destroyed: ", &session);
    }

    SessionDataInitializer(PeerLinker& server)
        : server(&server) {}
};
} // namespace
} // namespace p2p::plink

auto main(const int argc, const char* argv[]) -> int {
    using namespace p2p::plink;

    auto server = PeerLinker();
    auto initor = std::unique_ptr<ws::server::SessionDataInitializer>(new SessionDataInitializer(server));
    ensure(run(argc, argv, 8080, server, std::move(initor), "peer-linker"));
    return 0;
}
