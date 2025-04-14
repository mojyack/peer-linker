#include "macros/logger.hpp"
#include "peer-linker-protocol.hpp"
#include "protocol.hpp"
#include "server.hpp"
#include "util/string-map.hpp"

#define CUTIL_MACROS_PRINT_FUNC(...) LOG_ERROR(logger, __VA_ARGS__)
#include "macros/coop-unwrap.hpp"

namespace plink {
namespace {
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
        AuthorMismatched,

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
    "authenticator mismatched",              // AuthorMismatched
};

static_assert(Error::Limit == estr.size());

struct LinkRequestState {
    std::string   authenticator_name;
    net::PacketID packet_id;
};

struct Pad {
    std::string                     name;
    Session*                        session = nullptr;
    Pad*                            linked  = nullptr;
    std::optional<LinkRequestState> pending_link_request;
};

struct PeerLinker : Server {
    StringMap<Pad> pads;

    auto remove_pad(Pad* pad) -> coop::Async<void>;
    auto alloc_session() -> coop::Async<Session*> override;
    auto free_session(Session* ptr) -> coop::Async<void> override;
};

struct PeerLinkerSession : Session {
    PeerLinker* server;
    Pad*        pad = nullptr;

    auto handle_payload(net::Header header, net::BytesRef payload) -> coop::Async<bool> override;
};

auto PeerLinkerSession::handle_payload(const net::Header header, const net::BytesRef payload) -> coop::Async<bool> {
    auto& logger = server->logger;

    if(header.type == proto::ActivateSession::pt) {
        coop_ensure(handle_activation(payload, *server));
        goto finish;
    } else {
        coop_ensure(activated, "{}", estr[Error::NotActivated]);
    }

    switch(header.type) {
    case proto::RegisterPad::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::RegisterPad>(payload)));
        LOG_INFO(logger, "received pad register request name={}", request.name);

        coop_ensure(!request.name.empty(), "{}", estr[Error::EmptyPadName]);
        coop_ensure(pad == nullptr, "{}", estr[Error::AlreadyRegistered]);
        coop_ensure(server->pads.find(request.name) == server->pads.end(), "{}", estr[Error::PadFound]);

        LOG_INFO(logger, "pad {} registerd", request.name);
        pad = &server->pads.insert(std::pair{request.name, Pad{.name = request.name, .session = this}}).first->second;
    } break;
    case proto::UnregisterPad::pt: {
        LOG_INFO(logger, "received unregister request");

        coop_ensure(pad != nullptr, "{}", estr[Error::NotRegistered]);

        LOG_INFO(logger, "unregistering pad {}", pad->name);
        co_await server->remove_pad(pad);
        pad = nullptr;
    } break;
    case proto::Link::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::Link>(payload)));
        LOG_INFO(logger, "received pad link request to {}", request.requestee_name);

        coop_ensure(pad != nullptr, "{}", estr[Error::NotRegistered]);
        coop_ensure(pad->linked == nullptr, "{}", estr[Error::AlreadyLinked]);
        coop_ensure(!pad->pending_link_request, "{}", estr[Error::AuthInProgress]);
        const auto it = server->pads.find(request.requestee_name);
        coop_ensure(it != server->pads.end(), "{}", estr[Error::PadNotFound]);
        auto& requestee = it->second;

        LOG_INFO(logger, "sending auth request from {} to {}", pad->name, request.requestee_name);
        coop_ensure(co_await requestee.session->parser.send_packet(proto::Auth{pad->name, request.secret}));
        pad->pending_link_request = LinkRequestState{requestee.name, header.id};
        co_return true; // result is sent after auth_response
    } break;
    case proto::Unlink::pt: {
        LOG_INFO(logger, "received unlink request");

        coop_ensure(pad != nullptr, "{}", estr[Error::NotRegistered]);
        coop_ensure(pad->linked != nullptr, "{}", estr[Error::NotLinked]);

        LOG_INFO(logger, "unlinking pad {} and {}", pad->name, pad->linked->name);
        coop_ensure(co_await pad->linked->session->parser.send_packet(proto::Unlinked()));
        pad->linked->linked = nullptr;
        pad->linked         = nullptr;
    } break;
    case proto::AuthResponse::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::AuthResponse>(payload)));
        LOG_INFO(logger, "received link auth to name={} ok={}", request.requester_name, request.ok);

        coop_ensure(pad != nullptr, "{}", estr[Error::NotRegistered]);

        const auto it = server->pads.find(request.requester_name);
        coop_ensure(it != server->pads.end(), "{}", estr[Error::PadNotFound]);
        auto& requester = it->second;
        coop_ensure(requester.pending_link_request, "{}", estr[Error::AuthNotInProgress]);
        coop_ensure(pad->name == requester.pending_link_request->authenticator_name, "{}", estr[Error::AuthorMismatched]);

        coop_ensure(co_await requester.session->parser.send_packet(proto::Success(), requester.pending_link_request->packet_id));
        requester.pending_link_request.reset();
        if(request.ok) {
            LOG_INFO(logger, "linking {} and {}", pad->name, requester.name);
            pad->linked      = &requester;
            requester.linked = pad;
        }
        co_return true;
    } break;
    case proto::Payload::pt: {
        coop_ensure(pad != nullptr, "{}", estr[Error::NotRegistered]);
        coop_ensure(pad->linked != nullptr, "{}", estr[Error::NotLinked]);

        LOG_DEBUG(logger, "passthroughing packet from {} to {}", pad->name, pad->linked->name);
        // PRINT("packet type={} id={} size={}", header.type, header.id, header.size);
        // auto& ih = *(net::Header*)(payload.data());
        // PRINT("inner type={} id={} size={}", ih.type, ih.id, ih.size);
        // dump_hex(payload);
        coop_ensure(co_await pad->linked->session->parser.send_packet(proto::Payload::pt, payload.data(), payload.size()));
        co_return true;
    }
    default:
        coop_bail("unknown packet type {}", header.type);
    }

finish:
    coop_ensure(co_await parser.send_packet(proto::Success(), header.id));
    co_return true;
}

auto PeerLinker::remove_pad(Pad* const pad) -> coop::Async<void> {
    if(pad == nullptr) {
        co_return;
    }
    if(pad->linked != nullptr) {
        co_await pad->linked->session->parser.send_packet(proto::Unlinked());
        pad->linked->linked = nullptr;
    }
    pads.erase(pad->name);
}

auto PeerLinker::alloc_session() -> coop::Async<Session*> {
    auto& session  = *(new PeerLinkerSession());
    session.server = this;
    LOG_DEBUG(logger, "session created {}", &session);
    co_return &session;
}

auto PeerLinker::free_session(Session* const ptr) -> coop::Async<void> {
    auto& session = *std::bit_cast<PeerLinkerSession*>(ptr);
    co_await remove_pad(session.pad);
    delete &session;
    LOG_DEBUG(logger, "session destroyed {}", &session);
}
} // namespace
} // namespace plink

auto main(const int argc, const char* argv[]) -> int {
    using namespace plink;

    auto  server = PeerLinker();
    auto& logger = server.logger;
    logger.set_name_and_detect_loglevel("plink");
    ensure(run(argc, argv, 8080, server, "peer-linker"));
    return 0;
}
