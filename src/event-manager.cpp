#include <algorithm>
#include <utility>

#include "event-manager.hpp"
#include "macros/assert.hpp"
#include "util/event.hpp"

namespace p2p {
auto Events::eh_match(const uint32_t kind, const uint32_t id) -> auto {
    return [kind, id](const Handler& h) { return h.kind == kind && h.id == id; };
}

auto Events::register_callback(const uint32_t kind, const uint32_t id, const EventCallback callback) -> bool {
    if(debug) {
        line_print("new event handler registered kind: ", kind, " id: ", id);
    }

    auto value = std::optional<uint32_t>();
    {
        auto guard = std::lock_guard(lock);
        ensure(!drained);
        if(const auto i = std::ranges::find_if(notified, eh_match(kind, id)); i != notified.end()) {
            value = i->value;
            notified.erase(i);
        }
        if(!value) {
            handlers.emplace_back(Handler{
                .kind     = kind,
                .id       = id,
                .callback = callback,
            });
            return true;
        }
    }
    if(value) {
        callback(*value);
    }
    return true;
}

auto Events::wait_for(const uint32_t kind, const uint32_t id) -> std::optional<uint32_t> {
    {
        auto guard = std::lock_guard(lock);
        ensure(!drained);
        if(const auto i = std::ranges::find_if(notified, eh_match(kind, id)); i != notified.end()) {
            const auto value = i->value;
            notified.erase(i);
            return value;
        }
    }
    auto event = Event();
    auto value = uint32_t();
    ensure(register_callback(kind, id, [&event, &value](const uint32_t v) {value = v; event.notify(); }));
    event.wait();
    return value;
}

auto Events::invoke(uint32_t kind, const uint32_t id, const uint32_t value) -> void {
    if(debug) {
        if(id != no_id) {
            line_print("new event kind: ", kind, " id: ", id, " value: ", value);
        } else {
            line_print("new event kind: ", kind, " value: ", value);
        }
    }

    auto found = std::optional<Handler>();
    {
        auto guard = std::lock_guard(lock);
        if(const auto i = std::ranges::find_if(handlers, eh_match(kind, id)); i != handlers.end()) {
            found = *i;
            handlers.erase(i);
        }
        if(!found) {
            ensure(notified.size() < 32, "event queue is full, dropping notified event kind=", kind, " id=", id, " value=", value);
            notified.emplace_back(Handler{.kind = kind, .id = id, .value = value});
            return;
        }
    }
    found->callback(value);
}

auto Events::drain() -> bool {
    if(debug) {
        line_print("draining...");
    }

    {
        auto guard = std::lock_guard(lock);
        if(std::exchange(drained, true)) {
            return false;
        }
    }

loop:
    auto found = std::optional<Handler>();
    {
        auto guard = std::lock_guard(lock);
        if(handlers.empty()) {
            return true;
        }
        found = std::move(handlers.back());
        handlers.pop_back();
    }
    found->callback(drained_value);
    goto loop;
}

auto Events::is_drained() const -> bool {
    auto guard = std::lock_guard(lock);
    return drained;
}
} // namespace p2p
