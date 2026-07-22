#include "rid/identity.hpp"

#include <array>

#include "mbedtls/sha256.h"

namespace rid {
namespace {

constexpr char kHex[] = "0123456789ABCDEF";

uint8_t protocol_tag(Protocol protocol) {
    return protocol == Protocol::Gb46750 ? 0x00 : 0x01;
}

}  // namespace

TestIdentity derive_test_id(const DeviceId &device, std::string_view group_name,
                            uint32_t group_index, Protocol protocol) {
    const std::array<uint8_t, 4> index_be = {
        static_cast<uint8_t>(group_index >> 24),
        static_cast<uint8_t>(group_index >> 16),
        static_cast<uint8_t>(group_index >> 8),
        static_cast<uint8_t>(group_index),
    };
    const uint8_t tag = protocol_tag(protocol);
    std::array<uint8_t, 32> digest{};

    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    const bool ok = mbedtls_sha256_starts(&context, 0) == 0 &&
                    mbedtls_sha256_update(&context, device.bytes.data(), device.bytes.size()) == 0 &&
                    mbedtls_sha256_update(
                        &context, reinterpret_cast<const uint8_t *>(group_name.data()),
                        group_name.size()) == 0 &&
                    mbedtls_sha256_update(&context, index_be.data(), index_be.size()) == 0 &&
                    mbedtls_sha256_update(&context, &tag, sizeof(tag)) == 0 &&
                    mbedtls_sha256_finish(&context, digest.data()) == 0;
    mbedtls_sha256_free(&context);
    if (!ok) return TestIdentity{};

    TestIdentity identity{"TEST"};
    identity.value.reserve(20);
    for (size_t i = 0; i < 8; ++i) {
        identity.value.push_back(kHex[digest[i] >> 4]);
        identity.value.push_back(kHex[digest[i] & 0x0f]);
    }
    return identity;
}

}  // namespace rid
