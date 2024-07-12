#pragma once
#include "protocol.hpp"

namespace p2p::chub::proto {
struct Type {
    enum : uint16_t {
        Success,
        Error,
        Register,            // server <-  sender => (Success|Error)  register channel
        Unregister,          // server <-  sender => (Success|Error)  unregister channel
        GetChannels,         // server <-  receiver => (GetChannelsResponse|Error)  query registered channels
        GetChannelsResponse, // server ->  receiver => ()  registered channels
        PadRequest,          // server <-  receiver => (Success|Error)  ask server to send pad request to a channel
                             // server  -> sender => (PadRequestResponse) request new pad
        PadRequestResponse,  // server <-  sender => (Success|Error)  ask server to notify receiver creation of pad
                             // server  -> receiver => () registered pad name

        // marker
        Limit,
    };
};

struct Register : ::p2p::proto::Packet {
    // char channel_name[];
};

struct Unregister : ::p2p::proto::Packet {
    // char channel_name[];
};

struct GetChannels : ::p2p::proto::Packet {
};

struct GetChannelsResponse : ::p2p::proto::Packet {
    // char channels[]; // null-terminated string list
};

struct PadRequest : ::p2p::proto::Packet {
    // char channel_name[];
};

struct PadRequestResponse : ::p2p::proto::Packet {
    uint16_t ok;
    // char pad_name[];
};
} // namespace p2p::chub::proto
