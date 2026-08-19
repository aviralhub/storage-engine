#include "storage_engine/crc32.hpp"

#include <array>

namespace storage_engine {

uint32_t crc32(const void* data, std::size_t len) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            uint32_t c = static_cast<uint32_t>(i);
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[static_cast<std::size_t>(i)] = c;
        }
        return t;
    }();

    const auto* bytes = static_cast<const unsigned char*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace storage_engine
