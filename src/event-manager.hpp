#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace p2p {
using EventHandler = void(uint32_t value);

constexpr auto no_id    = uint32_t(-1);
constexpr auto no_value = uint32_t(-1);

struct EventHandlerInfo {
    uint32_t                    kind;
    uint32_t                    id;
    std::function<EventHandler> handler;
};

struct Events {
    std::mutex                    lock;
    std::vector<EventHandlerInfo> handlers;
    bool                          debug = false;

    auto invoke(uint32_t kind, uint32_t id, uint32_t value) -> void;
    auto add_handler(EventHandlerInfo info) -> void;
    auto drain() -> void;
};
} // namespace p2p
