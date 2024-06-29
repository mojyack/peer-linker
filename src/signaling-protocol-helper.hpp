#pragma once
#include <string_view>

#include "signaling-protocol.hpp"
#include "ws/impl.hpp"
#include "ws/misc.hpp"

namespace proto {
template <class T>
auto extract_payload(const std::span<const std::byte> payload) -> const T* {
    if(payload.size() <= sizeof(T)) {
        return nullptr;
    }
    return std::bit_cast<T*>(payload.data());
}

template <class T>
auto extract_last_string(const std::span<const std::byte> payload) -> std::string_view {
    const auto header = *std::bit_cast<proto::Packet*>(payload.data());
    return std::string_view((char*)(payload.data() + sizeof(T)), header.size - sizeof(T));
}

template <std::integral T>
inline auto add_parameter(std::vector<std::byte>& buffer, const T num) -> void {
    const auto prev_size = buffer.size();
    buffer.resize(prev_size + sizeof(T));
    *std::bit_cast<T*>(buffer.data() + prev_size) = num;
}

inline auto add_parameter(std::vector<std::byte>& buffer, const std::string_view str) -> void {
    const auto prev_size = buffer.size();
    buffer.resize(prev_size + str.size());
    memcpy(buffer.data() + prev_size, str.data(), str.size());
}

inline auto add_parameters(std::vector<std::byte>&) -> void {
}

template <class Arg, class... Args>
inline auto add_parameters(std::vector<std::byte>& buffer, Arg arg, Args... args) -> void {
    add_parameter(buffer, arg);
    add_parameters(buffer, args...);
}

template <class... Args>
inline auto send_packet(lws* wsi, Type type, Args... args) -> void {
    auto buffer = std::vector<std::byte>(sizeof(Packet));
    add_parameters(buffer, args...);
    *(std::bit_cast<Packet*>(buffer.data())) = Packet{uint16_t(buffer.size()), type};
    ws::write_back(wsi, buffer.data(), buffer.size());
}
} // namespace proto
