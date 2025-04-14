#pragma once
#include "net/common.hpp"

// client <-> (pad: server :pad) <-> client
namespace plink::proto {
// server <- client => (Result) create pad in server
struct RegisterPad {
    constexpr static auto pt = net::PacketType(0x03);

    SerdeFieldsBegin;
    std::string SerdeField(name);
    SerdeFieldsEnd;
};

// server <- client => (Result) delete pad in server
struct UnregisterPad {
    constexpr static auto pt = net::PacketType(0x04);
};

// server <- client => (Result) ask server to link self pad to another pad
struct Link {
    constexpr static auto pt = net::PacketType(0x05);

    SerdeFieldsBegin;
    std::string     SerdeField(requestee_name);
    net::BytesArray SerdeField(secret);
    SerdeFieldsEnd;
};

// server <- client => (Result) delete link
struct Unlink {
    constexpr static auto pt = net::PacketType(0x06);
};

// server -> client => () notify client to unlinked by other pad
struct Unlinked {
    constexpr static auto pt = net::PacketType(0x07);
};

// server -> client => (AuthResponse) ask client to whether a pad is linkable to his
struct Auth {
    constexpr static auto pt = net::PacketType(0x08);

    SerdeFieldsBegin;
    std::string     SerdeField(requester_name);
    net::BytesArray SerdeField(secret);
    SerdeFieldsEnd;
};

// server <- client => () accept pad linking
struct AuthResponse {
    constexpr static auto pt = net::PacketType(0x09);

    SerdeFieldsBegin;
    std::string SerdeField(requester_name);
    bool        SerdeField(ok);
    SerdeFieldsEnd;
};

// server <-> client => () payload to passthrough to the linked pad
struct Payload {
    constexpr static auto pt = net::PacketType(0x10);
};
} // namespace plink::proto
