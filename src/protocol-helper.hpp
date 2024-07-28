#pragma once
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "protocol.hpp"

namespace p2p::proto {
inline auto extract_header(const std::span<const std::byte> payload) -> const Packet* {
    if(payload.size() < sizeof(proto::Packet)) {
        return nullptr;
    }
    const auto& header = *std::bit_cast<proto::Packet*>(payload.data());
    if(header.size != payload.size()) {
        return nullptr;
    }
    return &header;
}

template <class T>
auto extract_payload(const std::span<const std::byte> payload) -> const T* {
    if(payload.size() < sizeof(T)) {
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
    std::memcpy(buffer.data() + prev_size, str.data(), str.size());
}

inline auto add_parameter(std::vector<std::byte>& buffer, const std::span<const std::byte> data) -> void {
    const auto prev_size = buffer.size();
    buffer.resize(prev_size + data.size());
    std::memcpy(buffer.data() + prev_size, data.data(), data.size());
}

inline auto add_parameters(std::vector<std::byte>&) -> void {
}

template <class Arg, class... Args>
inline auto add_parameters(std::vector<std::byte>& buffer, Arg arg, Args... args) -> void {
    add_parameter(buffer, arg);
    add_parameters(buffer, args...);
}

template <class... Args>
inline auto build_packet(uint16_t type, uint32_t id, Args... args) -> std::vector<std::byte> {
    auto buffer = std::vector<std::byte>(sizeof(Packet));
    add_parameters(buffer, args...);
    *(std::bit_cast<Packet*>(buffer.data())) = Packet{uint16_t(buffer.size()), type, id};
    return buffer;
}
} // namespace p2p::proto
