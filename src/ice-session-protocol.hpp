#pragma once
#include "peer-linker-protocol.hpp"

namespace p2p::ice::proto {
struct Type {
    enum : uint16_t {
        SetCandidates = ::p2p::plink::proto::Type::Limit,
        AddCandidates,
        GatheringDone,

        Limit,
    };
};

struct SetCandidates : ::p2p::proto::Packet {
    // char sdp[];
};

struct AddCandidates : ::p2p::proto::Packet {
    // char sdp[];
};

struct GatheringDone : ::p2p::proto::Packet {
};
} // namespace p2p::ice::proto
