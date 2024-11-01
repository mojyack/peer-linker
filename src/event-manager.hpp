#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>

namespace p2p {
using EventCallback = std::function<void(uint32_t value)>;

constexpr auto no_id         = uint32_t(-1);
constexpr auto no_value      = uint32_t(-1);
constexpr auto drained_value = uint32_t(-2);

class Events {
  private:
    struct Handler {
        uint32_t      kind;
        uint32_t      id;
        uint32_t      value;
        EventCallback callback;
    };

    mutable std::mutex  lock;
    std::deque<Handler> handlers;
    std::deque<Handler> notified;
    bool                drained = false;

    auto eh_match(const uint32_t kind, const uint32_t id) -> auto;

  public:
    // register_callback and wait_for are exclusive
    auto register_callback(uint32_t kind, uint32_t id, EventCallback callback) -> bool;
    auto wait_for(uint32_t kind, uint32_t id = no_id, bool report_drain = false) -> std::optional<uint32_t>;
    auto invoke(uint32_t kind, uint32_t id, uint32_t value) -> void;
    auto drain() -> bool;
    auto is_drained() const -> bool;
};
} // namespace p2p
