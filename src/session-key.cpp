#include <cstring>

#include "crypto/base64.hpp"
#include "crypto/hmac.hpp"
#include "macros/unwrap.hpp"
#include "util/span.hpp"

#include "session-key.hpp"

auto SessionKey::split_user_certificate_to_hash_and_content(const std::string_view cert) -> std::optional<std::array<std::string_view, 2>> {
    const auto lf = cert.find('\n');
    ensure(lf != cert.npos);
    const auto hash_str = cert.substr(0, lf);
    const auto content  = cert.substr(lf + 1);
    return std::array{hash_str, content};
}

auto SessionKey::generate_user_certificate(const std::string_view content) -> std::optional<std::string> {
    unwrap(hash, crypto::hmac::compute_hmac_sha256(secret, to_span(content)));
    const auto hash_str = crypto::base64::encode(hash);
    return build_string(hash_str, "\n", content);
}

auto SessionKey::verify_user_certificate_hash(const std::string_view hash_str, const std::string_view content) -> bool {
    ensure(hash_str.size() % 4 == 0, "not a base64 encoded string");
    const auto hash = crypto::base64::decode(hash_str);
    unwrap(computed_hash, crypto::hmac::compute_hmac_sha256(secret, to_span(content)));
    ensure(hash.size() == computed_hash.size(), "not a valid certification hash");
    ensure(std::memcmp(hash.data(), computed_hash.data(), hash.size()) == 0, "hash mismatched");
    return true;
}

SessionKey::SessionKey(std::vector<std::byte> secret)
    : secret(secret) {}
