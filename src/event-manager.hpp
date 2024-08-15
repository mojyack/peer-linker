#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace p2p {
using EventCallback = std::function<void(uint32_t value)>;

constexpr auto no_id    = uint32_t(-1);
constexpr auto no_value = uint32_t(-1);

class Events {
  private:
    struct Handler {
        uint32_t      kind;
        uint32_t      id;
        uint32_t      value;
        EventCallback callback;
    };

    std::mutex          lock;
    std::deque<Handler> handlers;
    std::deque<Handler> notified;

    auto eh_match(const uint32_t kind, const uint32_t id) -> auto;

  public:
    bool debug = false;

    // register_callback and wait_for are exclusive
    auto register_callback(uint32_t kind, uint32_t id, EventCallback callback) -> void;
    auto wait_for(uint32_t kind, uint32_t id) -> uint32_t;
    auto invoke(uint32_t kind, uint32_t id, uint32_t value) -> void;
    auto drain() -> void;
};
} // namespace p2p
