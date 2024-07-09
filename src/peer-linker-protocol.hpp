#pragma once
#include <cstdint>

namespace p2p::proto {
// client <-> (pad: server :pad) <-> client

struct Type {
    enum : uint16_t {
        Success,
        Error,
        // Every packet sent from the client to the server returns a Success or Error packet with the corresponding ID.
        // The ID of packets sent from the server to the client, excluding result packets, must be ignored
        Register,         // server <-  client  create pad in server
        Unregister,       // server <-  client  delete pad in server
        Link,             // server <-  client  ask server to link self pad to another pad
        Unlink,           // server <-  client  delete link
        LinkSuccess,      // server  -> client  notify client to linked successfully
        LinkDenied,       // server  -> client  notify client to link denied
        Unlinked,         // server  -> client  notify client to unlinked by other pad
        LinkAuth,         // server  -> client  ask client to whether a pad is linkable to his
        LinkAuthResponse, // server <-  client  accept pad linking

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
    uint32_t id;
} __attribute__((packed));

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
