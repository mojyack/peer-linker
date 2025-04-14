#pragma once
#include "net/common.hpp"

namespace plink::proto {
// server <- sender   => (Result) register channel
struct RegisterChannel {
    constexpr static auto pt = net::PacketType(0x03);

    SerdeFieldsBegin;
    std::string SerdeField(name);
    SerdeFieldsEnd;
};

// server <- sender   => (Result) unregister channel
struct UnregisterChannel {
    constexpr static auto pt = net::PacketType(0x04);

    SerdeFieldsBegin;
    std::string SerdeField(name);
    SerdeFieldsEnd;
};

// server <- receiver => (Channels) query registered channels
struct GetChannels {
    constexpr static auto pt = net::PacketType(0x05);
};

// server -> receiver => () registered channels
struct Channels {
    constexpr static auto pt = net::PacketType(0x06);

    SerdeFieldsBegin;
    std::vector<std::string> SerdeField(channels);
    SerdeFieldsEnd;
};

// server <- receiver => (PadCreated) ask server to send pad request to a channel
// server -> sender   => (PadCreated) request new pad
struct RequestPad {
    constexpr static auto pt = net::PacketType(0x07);

    SerdeFieldsBegin;
    std::string SerdeField(channel_name);
    SerdeFieldsEnd;
};

// server <- sender   => (Result) ask server to notify receiver creation of pad
// server -> receiver => () registered pad name
struct PadCreated {
    constexpr static auto pt = net::PacketType(0x08);

    SerdeFieldsBegin;
    std::string SerdeField(channel_name);
    std::string SerdeField(pad_name); // empty name indicates error
    SerdeFieldsEnd;
};
} // namespace plink::proto
