#pragma once

#include <cstddef>
#include <cstdint>

namespace storage_engine {

uint32_t crc32(const void* data, std::size_t len);

}  // namespace storage_engine
