#pragma once

#include "storage_engine/disk_manager.hpp"

#include <cstdint>

namespace storage_engine {

constexpr int32_t kMetaMagic = 0x4B565354;  // "KVST"
constexpr std::size_t kMaxValueSize = 112;
constexpr std::size_t kMaxLeafKeys = 32;
constexpr std::size_t kMaxInternalKeys = 256;
constexpr std::size_t kMinLeafKeys = kMaxLeafKeys / 2;
constexpr std::size_t kMinInternalKeys = kMaxInternalKeys / 2;

struct PageHeader {
    int32_t is_leaf;
    int32_t key_count;
};

struct MetaPage {
    int32_t magic;
    int32_t root_page_id;
};

// +1 slot: a node overflows by one before it splits
struct LeafPage {
    int32_t is_leaf;
    int32_t key_count;
    int32_t next_leaf_id;
    int32_t _reserved;
    int64_t keys[kMaxLeafKeys + 1];
    char values[kMaxLeafKeys + 1][kMaxValueSize];
};

struct InternalPage {
    int32_t is_leaf;
    int32_t key_count;
    int32_t _reserved1;
    int32_t _reserved2;
    int64_t keys[kMaxInternalKeys + 1];
    int32_t children[kMaxInternalKeys + 2];
};

static_assert(sizeof(MetaPage) <= kPageSize, "MetaPage must fit in one page");
static_assert(sizeof(LeafPage) <= kPageSize, "LeafPage must fit in one page");
static_assert(sizeof(InternalPage) <= kPageSize, "InternalPage must fit in one page");

}  // namespace storage_engine
