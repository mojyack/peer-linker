#pragma once
#include "peer-linker-protocol.hpp"

namespace p2p::ice::proto {
struct Type {
    enum : uint16_t {
        SessionDescription = ::p2p::plink::proto::Type::Limit,
        Candidate,
        GatheringDone,

        Limit,
    };
};

struct SessionDescription : ::p2p::proto::Packet {
    // char desc[];
};

struct Candidate : ::p2p::proto::Packet {
    // char desc[];
};

struct GatheringDone : ::p2p::proto::Packet {
};
} // namespace p2p::ice::proto
