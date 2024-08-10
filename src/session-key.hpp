#pragma once
#include <array>
#include <optional>
#include <string_view>
#include <vector>

class SessionKey {
  private:
    std::vector<std::byte> secret;

  public:
    static auto split_user_certificate_to_hash_and_content(std::string_view cert) -> std::optional<std::array<std::string_view, 2>>;

    auto generate_user_certificate(std::string_view content) -> std::optional<std::string>;
    auto verify_user_certificate_hash(std::string_view hash_str, std::string_view content) -> bool;

    SessionKey(std::vector<std::byte> secret);
};
