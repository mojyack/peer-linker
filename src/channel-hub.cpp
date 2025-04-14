#include "channel-hub-protocol.hpp"
#include "macros/logger.hpp"
#include "protocol.hpp"
#include "server.hpp"

#define CUTIL_MACROS_PRINT_FUNC(...) LOG_ERROR(logger, __VA_ARGS__)
#include "macros/coop-unwrap.hpp"

namespace plink {
namespace {
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

struct ChannelHub;
struct ChannelHubSession;

struct PadRequest {
    ChannelHubSession* requester;
    net::PacketID      packet_id;
};

struct Channel {
    std::string             name;
    ChannelHubSession*      session;
    std::vector<PadRequest> requests;
};

struct ChannelHub : Server {
    std::vector<Channel> channels;

    auto alloc_session() -> coop::Async<Session*> override;
    auto free_session(Session* ptr) -> coop::Async<void> override;
};

struct ChannelHubSession : Session {
    ChannelHub* server;

    auto handle_payload(net::Header header, net::BytesRef payload) -> coop::Async<bool> override;
};

auto cond(const std::string& name) -> auto {
    return [&name](Channel& ch) { return ch.name == name; };
}

auto ChannelHubSession::handle_payload(const net::Header header, const net::BytesRef payload) -> coop::Async<bool> {
    auto& logger = server->logger;

    if(header.type == proto::ActivateSession::pt) {
        coop_ensure(handle_activation(payload, *server));
        goto finish;
    } else {
        coop_ensure(activated, "{}", estr[Error::NotActivated]);
    }

    switch(header.type) {
    case proto::RegisterChannel::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::RegisterChannel>(payload)));
        LOG_INFO(logger, "received channel register request name={}", request.name);

        coop_ensure(!request.name.empty(), "{}", estr[Error::EmptyChannelName]);
        coop_ensure(std::ranges::find_if(server->channels, cond(request.name)) == server->channels.end(), "{}", estr[Error::ChannelFound]);

        LOG_INFO(logger, "channel {} registerd", request.name);
        server->channels.push_back(Channel{request.name, this});
    } break;
    case proto::UnregisterChannel::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::UnregisterChannel>(payload)));
        LOG_INFO(logger, "received channel unregister request name={}", request.name);

        const auto it = std::ranges::find_if(server->channels, cond(request.name));
        coop_ensure(it != server->channels.end(), "{}", estr[Error::ChannelNotFound]);
        auto& channel = *it;
        coop_ensure(channel.session == this, "{}", estr[Error::SenderMismatch]);

        LOG_INFO(logger, "unregistering channel {}", channel.name);
        server->channels.erase(it);
    } break;
    case proto::GetChannels::pt: {
        LOG_INFO(logger, "received channel list request");
        auto payload = std::vector<std::string>();
        for(auto& channel : server->channels) {
            payload.push_back(channel.name);
        }
        coop_ensure(co_await parser.send_packet(proto::Channels{std::move(payload)}, header.id));
        co_return true;
    } break;
    case proto::RequestPad::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::RequestPad>(payload)));
        LOG_INFO(logger, "received pad request for channel={}", request.channel_name);

        const auto it = std::ranges::find_if(server->channels, cond(request.channel_name));
        coop_ensure(it != server->channels.end(), "{}", estr[Error::ChannelNotFound]);
        auto& channel = *it;

        coop_ensure(co_await channel.session->parser.send_packet(proto::RequestPad{request.channel_name}));
        channel.requests.push_back({this, header.id});
        PRINT("name={} size={}", channel.name, channel.requests.size());
        co_return true;
    } break;
    case proto::PadCreated::pt: {
        coop_unwrap(request, (serde::load<net::BinaryFormat, proto::PadCreated>(payload)));
        LOG_INFO(logger, "received pad request response channel={} name={}", request.channel_name, request.pad_name);

        const auto it = std::ranges::find_if(server->channels, cond(request.channel_name));
        coop_ensure(it != server->channels.end(), "{}", estr[Error::ChannelNotFound]);
        auto& channel = *it;
        coop_ensure(channel.session == this, "{}", estr[Error::SenderMismatch]);

        coop_ensure(!channel.requests.empty(), "{}", estr[Error::RequesterNotFound]);
        const auto pad_request = channel.requests.front();
        channel.requests.erase(channel.requests.begin());

        LOG_INFO(logger, "sending pad created name={}", request.pad_name);
        coop_ensure(co_await pad_request.requester->parser.send_packet(proto::PadCreated::pt, payload.data(), payload.size(), pad_request.packet_id));
    } break;
    default:
        coop_bail("unknown command {}", int(header.type));
    }

finish:
    coop_ensure(co_await parser.send_packet(proto::Success(), header.id));
    co_return true;
}

auto ChannelHub::alloc_session() -> coop::Async<Session*> {
    auto& session  = *(new ChannelHubSession());
    session.server = this;
    LOG_DEBUG(logger, "session created {}", &session);
    co_return &session;
}

auto ChannelHub::free_session(Session* const ptr) -> coop::Async<void> {
    auto& session = *std::bit_cast<ChannelHubSession*>(ptr);

    // remove hosting channels
    for(auto i = channels.begin(); i != channels.end();) {
        auto& channel = *i;
        if(channel.session != &session) {
            // cancel requests from this session
            for(auto r = channel.requests.end(); r < channel.requests.end();) {
                r = r->requester == &session ? channel.requests.erase(r) : r + 1;
            }
            i += 1;
            continue;
        }
        LOG_INFO(logger, "unregistering channel {}", channel.name);
        // cancel requests to this session
        for(const auto& request : channel.requests) {
            coop_ensure(co_await request.requester->parser.send_packet(proto::Error(), request.packet_id));
        }
        i = channels.erase(i);
    }

    delete &session;
    LOG_DEBUG(logger, "session destroyed {}", &session);
}
} // namespace
} // namespace plink

auto main(const int argc, const char* argv[]) -> int {
    using namespace plink;

    auto  server = ChannelHub();
    auto& logger = server.logger;
    logger.set_name_and_detect_loglevel("chub");
    ensure(run(argc, argv, 8081, server, "channel-hub"));
    return 0;
}
