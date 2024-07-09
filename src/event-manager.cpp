#include <optional>

#include "event-manager.hpp"
#include "macros/unwrap.hpp"
#include "util/assert.hpp"

namespace p2p::ice {
auto IceEvents::invoke(uint32_t kind, const uint32_t id, const uint32_t value) -> void {
    if(id != no_id) {
        PRINT("new event kind: ", kind, " id: ", id, " value: ", value);
    } else {
        PRINT("new event kind: ", kind, " value: ", value);
    }

    auto found = std::optional<IceEventHandlerInfo>();
    {
        auto guard = std::lock_guard(lock);
        for(auto i = handlers.begin(); i < handlers.end(); i += 1) {
            if(i->kind == kind && i->id == id) {
                found = std::move(*i);
                handlers.erase(i);
                break;
            }
        }
    }
    unwrap_on(info, found, "unhandled event");
    info.handler(value);
}

auto IceEvents::add_handler(IceEventHandlerInfo info) -> void {
    auto guard = std::lock_guard(lock);
    handlers.push_back(info);
}

auto IceEvents::drain() -> void {
loop:
    auto found = std::optional<IceEventHandlerInfo>();
    {
        auto guard = std::lock_guard(lock);
        if(handlers.empty()) {
            return;
        }
        found = std::move(handlers.back());
        handlers.pop_back();
    }
    found->handler(0);
    goto loop;
}
} // namespace p2p::ice
