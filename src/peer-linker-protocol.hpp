#pragma once
#include "protocol.hpp"

namespace p2p::plink::proto {
// client <-> (pad: server :pad) <-> client

struct Type {
    enum : uint16_t {
        Success,
        Error,
        Register,         // server <-  client => (Success|Error) create pad in server
        Unregister,       // server <-  client => (Success|Error) delete pad in server
        Link,             // server <-  client => (Success|Error) ask server to link self pad to another pad
        Unlink,           // server <-  client => (Success|Error) delete link
        LinkSuccess,      // server  -> client => () notify client to linked successfully
        LinkDenied,       // server  -> client => () notify client to link denied
        Unlinked,         // server  -> client => () notify client to unlinked by other pad
        LinkAuth,         // server  -> client => (LinkAuthResponse) ask client to whether a pad is linkable to his
        LinkAuthResponse, // server <-  client => (Success|Error) accept pad linking

        // marker
        Limit,
    };
};

struct Register : ::p2p::proto::Packet {
    // char name[];
};

struct Link : ::p2p::proto::Packet {
    // char requestee_name[];
};

struct Unlink : ::p2p::proto::Packet {
};

struct LinkAuth : ::p2p::proto::Packet {
    // char requester_name[];
};

struct LinkAuthResponse : ::p2p::proto::Packet {
    uint16_t ok;
    // char requester_name[];
};
} // namespace p2p::plink::proto
