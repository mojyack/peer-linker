#pragma once
#include <cstdint>

namespace p2p::proto {
// client <-> (peer: server :peer) <-> client

struct Type {
    enum : uint16_t {
        Success,          // server <-> client
        Error,            // server <-> cleint
        Register,         // server <-  client  create peer in server
        Unregister,       // server <-  client  delete peer in server
        Link,             // server <-  client  ask server to link self to another peer
        Unlink,           // server <-  client  delete link
        Unlinked,         // server  -> client  notify client to unlinked by other pad
        LinkAuth,         // server  -> client  ask client to whether this peer is linkable to him
        LinkAuthResponse, // server <-  client  accept peer to link

        // following commands are valid only when linked
        SetCandidates,
        AddCandidates,
        GatheringDone,

        // marker
        Limit,
    };
};

struct Packet {
    uint16_t size; // total size in bytes, including this header
    uint16_t type;
};

struct Register : Packet {
    // char name[];
};

struct Link : Packet {
    // char target_name[];
};

struct Unlink : Packet {
};

struct LinkAuth : Packet {
    // char requestee_name[];
};

struct LinkAuthResponse : Packet {
    uint16_t ok;
    // char requester_name[];
};

struct SetCandidates : Packet {
    // char sdp[];
};

struct AddCandidates : Packet {
    // char sdp[];
};

struct GatheringDone : Packet {
};
} // namespace p2p::proto
