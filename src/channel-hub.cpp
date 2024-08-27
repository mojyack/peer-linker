#include "channel-hub-protocol.hpp"
#include "macros/unwrap.hpp"
#include "server.hpp"
#include "util/string-map.hpp"

namespace p2p::chub {
namespace {
struct ChannelHub;
struct ChannelHubSession;

struct Channel {
    std::string        name;
    ChannelHubSession* session;
};

struct Error {
    enum {
        NotActivated = 0,
        EmptyChannelName,
        ChannelFound,
        ChannelNotFound,
        SenderMismatch,
        AnotherRequestPending,
        RequesterNotFound,

        Limit,
    };
};

const auto estr = std::array{
    "session is not activated",                  // NotActivated
    "empty channel name",                        // EmptyChannelName
    "channel with that name already registered", // ChannelFound
    "no such channel registered",                // ChannelNotFound
    "channel not registered by the sender",      // SenderMismatch
    "another request in progress",               // AnotherRequestPending
    "requester not found",                       // RequesterNotFound
};

static_assert(Error::Limit == estr.size());

struct ChannelHubSession : Session {
    ChannelHub* server;
    lws*        wsi;

    auto handle_payload(std::span<const std::byte> payload) -> bool override;
};

struct PendingRequest {
    ChannelHubSession* requester;
    ChannelHubSession* requestee;
};

struct ChannelHub : Server {
    StringMap<Channel>                           channels;
    std::unordered_map<uint32_t, PendingRequest> pending_requests;
    uint32_t                                     packet_id;
};

auto ChannelHubSession::handle_payload(const std::span<const std::byte> payload) -> bool {
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
    case ::p2p::proto::Type::Success:
    case ::p2p::proto::Type::Error:
        line_warn("unexpected packet");
        return true;
    case proto::Type::Register: {
        const auto name = p2p::proto::extract_last_string<proto::Register>(payload);
        print("received channel register request name:", name);

        ensure(!name.empty(), estr[Error::EmptyChannelName]);
        ensure(server->channels.find(name) == server->channels.end(), estr[Error::ChannelFound]);

        print("channel ", name, " registerd");
        server->channels.insert(std::pair{name, Channel{std::string(name), this}});
    } break;
    case proto::Type::Unregister: {
        const auto name = p2p::proto::extract_last_string<proto::Unregister>(payload);
        print("received channel unregister request name: ", name);

        const auto it = server->channels.find(name);
        ensure(it != server->channels.end(), estr[Error::ChannelNotFound]);
        auto& channel = it->second;
        ensure(channel.session == this, estr[Error::SenderMismatch]);

        print("unregistering channel ", channel.name);
        server->channels.erase(it);
    } break;
    case proto::Type::GetChannels: {
        print("received channel list request");
        auto payload = std::vector<std::byte>();
        for(auto it = server->channels.begin(); it != server->channels.end(); it = std::next(it)) {
            const auto& name      = it->second.name;
            const auto  prev_size = payload.size();
            payload.resize(prev_size + name.size() + 1);
            std::memcpy(payload.data() + prev_size, name.data(), name.size() + 1);
        }

        ensure(server->send_to(wsi, proto::Type::GetChannelsResponse, header.id, payload));
        return true;
    } break;
    case proto::Type::PadRequest: {
        const auto name = p2p::proto::extract_last_string<proto::PadRequest>(payload);
        print("received pad request for channel: ", name);

        // check if another request is pending
        for(auto i = server->pending_requests.begin(); i != server->pending_requests.end(); i = std::next(i)) {
            ensure(i->second.requester != this, estr[Error::AnotherRequestPending]);
        }

        const auto it = server->channels.find(name);
        ensure(it != server->channels.end(), estr[Error::ChannelNotFound]);
        auto& channel = it->second;

        const auto id = server->packet_id += 1;
        ensure(server->send_to(channel.session->wsi, proto::Type::PadRequest, id, name));
        server->pending_requests.insert({id, PendingRequest{.requester = this, .requestee = channel.session}});
    } break;
    case proto::Type::PadRequestResponse: {
        print("received pad request response");

        unwrap(packet, p2p::proto::extract_payload<proto::PadRequestResponse>(payload));
        const auto pad_name = p2p::proto::extract_last_string<proto::PadRequestResponse>(payload);

        const auto request_it = server->pending_requests.find(header.id);
        ensure(request_it != server->pending_requests.end(), estr[Error::RequesterNotFound]);
        const auto request = request_it->second;
        server->pending_requests.erase(request_it);

        print("sending pad name ok: ", packet.ok, " pad_name: ", pad_name);
        ensure(server->send_to(request.requester->wsi, proto::Type::PadRequestResponse, 0, packet.ok, pad_name));
    } break;
    default: {
        bail("unknown command ", int(header.type));
    }
    }

finish:
    ensure(server->send_to(wsi, ::p2p::proto::Type::Success, header.id));
    return true;
}

struct SessionDataInitializer : ws::server::SessionDataInitializer {
    ChannelHub* server;

    auto get_size() -> size_t override {
        return sizeof(ChannelHubSession);
    }

    auto init(void* const ptr, lws* wsi) -> void override {
        print("session created: ", ptr);
        auto& session  = *new(ptr) ChannelHubSession();
        session.server = server;
        session.wsi    = wsi;
    }

    auto deinit(void* const ptr) -> void override {
        print("session destroyed: ", ptr);
        auto& session = *std::bit_cast<ChannelHubSession*>(ptr);

        // remove corresponding channels
        std::erase_if(server->channels, [&session](const auto& p) { return p.second.session == &session; });

        // remove from pending list
        auto& requests = server->pending_requests;
        for(auto i = requests.begin(); i != requests.end(); i = std::next(i)) {
            const auto& request = i->second;
            if(request.requester == &session) {
                // pad requester has gone.
                // delete request
                requests.erase(i);
                break;
            } else if(request.requestee == &session) {
                // pad requestee has gone.
                // delete request and send fail to requester
                server->send_to(request.requester->wsi, proto::Type::PadRequestResponse, 0, uint16_t(0));
                requests.erase(i);
                break;
            }
        }

        session.~ChannelHubSession();
    }

    SessionDataInitializer(ChannelHub& server)
        : server(&server) {}
};

auto run(const int argc, const char* argv[]) -> bool {
    auto server = ChannelHub();
    auto initor = std::unique_ptr<ws::server::SessionDataInitializer>(new SessionDataInitializer(server));
    ensure(run(argc, argv, 8081, server, std::move(initor), "channel-hub"));
    return true;
}
} // namespace
} // namespace p2p::chub

auto main(const int argc, const char* argv[]) -> int {
    return p2p::chub::run(argc, argv) ? 0 : 1;
}
