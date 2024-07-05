#pragma once
#include <functional>
#include <vector>

namespace p2p::ice {
using EventHandler = void(uint32_t value);

constexpr auto no_id    = uint32_t(-1);
constexpr auto no_value = uint32_t(-1);

struct IceEventHandlerInfo {
    uint32_t                    kind;
    uint32_t                    id;
    std::function<EventHandler> handler;
};

struct IceEvent {
    uint32_t kind;
    uint32_t id;
    uint32_t value;
};

struct IceEvents {
    std::mutex                       lock;
    std::vector<IceEventHandlerInfo> handlers;

    auto invoke(uint32_t kind, uint32_t id, uint32_t value) -> void;
    auto add_handler(IceEventHandlerInfo info) -> void;
    auto drain() -> void;
};
} // namespace p2p::ice
