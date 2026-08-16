#pragma once

#include "storage_engine/buffer_pool.hpp"
#include "storage_engine/page_layout.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace storage_engine {

class DiskBPlusTree {
public:
    explicit DiskBPlusTree(BufferPool& pool);

    void insert(int64_t key, const std::string& value);
    std::optional<std::string> get(int64_t key) const;
    std::vector<std::pair<int64_t, std::string>> rangeScan(int64_t low, int64_t high) const;
    bool remove(int64_t key);

private:
    BufferPool& pool_;

    struct SplitResult {
        int64_t separator_key;
        int32_t new_right_id;
    };
    struct PageInfo {
        bool is_leaf;
        std::size_t key_count;
    };
    enum class RemoveOutcome { NotFound, Ok, Underflow };

    int32_t getRootPageId() const;
    void setRootPageId(int32_t id);
    int32_t findLeafId(int64_t key) const;
    PageInfo inspect(int32_t page_id) const;

    std::optional<SplitResult> insertInto(int32_t page_id, int64_t key, const std::string& value);
    SplitResult splitLeaf(int32_t leftId, LeafPage* left);
    SplitResult splitInternal(int32_t leftId, InternalPage* left);

    RemoveOutcome removeFrom(int32_t page_id, int64_t key);
    void borrowFromLeft(int32_t parentId, std::size_t childIndex);
    void borrowFromRight(int32_t parentId, std::size_t childIndex);
    void mergeChildren(int32_t parentId, std::size_t leftIndex);
};

}  // namespace storage_engine
