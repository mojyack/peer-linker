#include "macros/unwrap.hpp"
#include "session-key.hpp"
#include "util/argument-parser.hpp"
#include "util/file-io.hpp"
#include "util/span.hpp"

namespace {
auto generate_cert(const char* const secret_file, const char* const content_file) -> bool {
    unwrap(secret, read_file(secret_file));
    unwrap(content, read_file(content_file));
    auto key = SessionKey(secret);
    unwrap(cert, key.generate_user_certificate(from_span(content)));
    printf("%s", cert.data());
    return true;
}

auto verify_cert(const char* const secret_file, const char* const content_file) -> bool {
    unwrap(secret, read_file(secret_file));
    unwrap(cert, read_file(content_file));
    auto key = SessionKey(secret);
    unwrap(parsed, key.split_user_certificate_to_hash_and_content(from_span(cert)));
    const auto [hash_str, content] = parsed;
    return key.verify_user_certificate_hash(hash_str, content);
}
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    auto secret = (const char*)(nullptr);
    auto file   = (const char*)(nullptr);
    auto verify = false;
    auto help   = false;
    auto parser = args::Parser<>();
    parser.kwflag(&verify, {"-d", "--verify"}, "verify user certificate");
    parser.kwflag(&help, {"-h", "--help"}, "print this help message", {.no_error_check = true});
    parser.arg(&secret, "SECRET_FILE", "path to key file");
    parser.arg(&file, "TARGET_FILE", "path to data or cert file");
    if(!parser.parse(argc, argv) || help) {
        print("usage: session-key-util ", parser.get_help());
        return 1;
    }

    if(!verify) {
        return generate_cert(secret, file) ? 0 : 1;
    } else {
        print(verify_cert(secret, file) ? "ok" : "fail");
        return 0;
    }
}
