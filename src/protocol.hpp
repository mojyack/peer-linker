#pragma once
#include "net/common.hpp"

namespace plink::proto {
// server -> client => () indicate success
struct Success {
    constexpr static auto pt = net::PacketType(0x00);
};

// server -> client => () indicate error
struct Error {
    constexpr static auto pt = net::PacketType(0x01);
};

// server <- client => (Success) send user certificate to active session
struct ActivateSession {
    constexpr static auto pt = net::PacketType(0x02);

    SerdeFieldsBegin;
    std::string SerdeField(user_certificate);
    SerdeFieldsEnd;
};
} // namespace plink::proto
