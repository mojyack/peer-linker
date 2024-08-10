#include "macros/unwrap.hpp"
#include "session-key.hpp"
#include "util/argument-parser.hpp"
#include "util/file-io.hpp"
#include "util/span.hpp"

namespace {
auto generate_cert(const char* const secret_file, const char* const content_file) -> bool {
    unwrap_ob(secret, read_file(secret_file));
    unwrap_ob(content, read_file(content_file));
    auto key = SessionKey(secret);
    unwrap_ob(cert, key.generate_user_certificate(from_span(content)));
    printf("%s", cert.data());
    return true;
}

auto verify_cert(const char* const secret_file, const char* const content_file) -> bool {
    unwrap_ob(secret, read_file(secret_file));
    unwrap_ob(cert, read_file(content_file));
    auto key = SessionKey(secret);
    unwrap_ob(parsed, key.split_user_certificate_to_hash_and_content(from_span(cert)));
    const auto [hash_str, content] = parsed;
    return key.verify_user_certificate_hash(hash_str, content);
}
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    auto secret = (const char*)(nullptr);
    auto file   = (const char*)(nullptr);
    auto verify = false;
    auto help   = false;
    auto parser = args::Parser();
    parser.kwarg(&verify, {"-d", "--verify"}, {"", "verify user certificate", args::State::Initialized});
    parser.kwarg(&help, {"-h", "--help"}, {.arg_desc = "print this help message", .state = args::State::Initialized, .no_error_check = true});
    parser.arg(&secret, {"SECRET_FILE"});
    parser.arg(&file, {"TARGET_FILE"});
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
