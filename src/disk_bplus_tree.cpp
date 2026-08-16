#include "storage_engine/disk_bplus_tree.hpp"

#include <cstring>
#include <stdexcept>

namespace storage_engine {

namespace {
std::size_t minKeysFor(bool is_leaf) {
    return is_leaf ? kMinLeafKeys : kMinInternalKeys;
}
}  // namespace

DiskBPlusTree::DiskBPlusTree(BufferPool& pool) : pool_(pool) {
    if (pool_.pageCount() == 0) {
        auto [metaId, raw] = pool_.newPage();
        auto* meta = reinterpret_cast<MetaPage*>(raw);
        meta->magic = kMetaMagic;
        meta->root_page_id = kInvalidPageId;
        pool_.unpinPage(metaId, true);
    } else {
        char* raw = pool_.fetchPage(0);
        auto* meta = reinterpret_cast<MetaPage*>(raw);
        bool ok = meta->magic == kMetaMagic;
        pool_.unpinPage(0, false);
        if (!ok) {
            throw std::runtime_error("DiskBPlusTree: bad meta page magic number");
        }
    }
}

int32_t DiskBPlusTree::getRootPageId() const {
    char* raw = pool_.fetchPage(0);
    int32_t root = reinterpret_cast<MetaPage*>(raw)->root_page_id;
    pool_.unpinPage(0, false);
    return root;
}

void DiskBPlusTree::setRootPageId(int32_t id) {
    char* raw = pool_.fetchPage(0);
    reinterpret_cast<MetaPage*>(raw)->root_page_id = id;
    pool_.unpinPage(0, true);
}

DiskBPlusTree::PageInfo DiskBPlusTree::inspect(int32_t page_id) const {
    char* raw = pool_.fetchPage(page_id);
    auto* header = reinterpret_cast<PageHeader*>(raw);
    PageInfo info{header->is_leaf != 0, static_cast<std::size_t>(header->key_count)};
    pool_.unpinPage(page_id, false);
    return info;
}

int32_t DiskBPlusTree::findLeafId(int64_t key) const {
    int32_t pageId = getRootPageId();
    if (pageId == kInvalidPageId) {
        return kInvalidPageId;
    }

    while (true) {
        char* raw = pool_.fetchPage(pageId);
        auto* header = reinterpret_cast<PageHeader*>(raw);
        if (header->is_leaf) {
            pool_.unpinPage(pageId, false);
            return pageId;
        }
        auto* internal = reinterpret_cast<InternalPage*>(raw);
        std::size_t idx = 0;
        while (idx < static_cast<std::size_t>(internal->key_count) && key >= internal->keys[idx]) {
            idx++;
        }
        int32_t next = internal->children[idx];
        pool_.unpinPage(pageId, false);
        pageId = next;
    }
}

DiskBPlusTree::SplitResult DiskBPlusTree::splitLeaf(int32_t leftId, LeafPage* left) {
    (void)leftId;
    auto [rightId, rightRaw] = pool_.newPage();
    auto* right = reinterpret_cast<LeafPage*>(rightRaw);
    right->is_leaf = 1;

    std::size_t mid = (static_cast<std::size_t>(left->key_count) + 1) / 2;
    std::size_t rightCount = 0;
    for (std::size_t i = mid; i < static_cast<std::size_t>(left->key_count); ++i) {
        right->keys[rightCount] = left->keys[i];
        std::memcpy(right->values[rightCount], left->values[i], kMaxValueSize);
        rightCount++;
    }
    right->key_count = static_cast<int32_t>(rightCount);
    right->next_leaf_id = left->next_leaf_id;

    left->key_count = static_cast<int32_t>(mid);
    left->next_leaf_id = rightId;

    int64_t separator = right->keys[0];
    pool_.unpinPage(rightId, true);
    return SplitResult{separator, rightId};
}

DiskBPlusTree::SplitResult DiskBPlusTree::splitInternal(int32_t leftId, InternalPage* left) {
    (void)leftId;
    auto [rightId, rightRaw] = pool_.newPage();
    auto* right = reinterpret_cast<InternalPage*>(rightRaw);
    right->is_leaf = 0;

    std::size_t mid = static_cast<std::size_t>(left->key_count) / 2;
    int64_t separator = left->keys[mid];

    std::size_t rightCount = 0;
    for (std::size_t i = mid + 1; i < static_cast<std::size_t>(left->key_count); ++i) {
        right->keys[rightCount++] = left->keys[i];
    }
    std::size_t rightChildCount = 0;
    for (std::size_t i = mid + 1; i <= static_cast<std::size_t>(left->key_count); ++i) {
        right->children[rightChildCount++] = left->children[i];
    }
    right->key_count = static_cast<int32_t>(rightCount);
    left->key_count = static_cast<int32_t>(mid);

    pool_.unpinPage(rightId, true);
    return SplitResult{separator, rightId};
}

std::optional<DiskBPlusTree::SplitResult> DiskBPlusTree::insertInto(int32_t page_id, int64_t key,
                                                                     const std::string& value) {
    char* raw = pool_.fetchPage(page_id);
    bool isLeaf = reinterpret_cast<PageHeader*>(raw)->is_leaf != 0;

    if (isLeaf) {
        auto* leaf = reinterpret_cast<LeafPage*>(raw);
        std::size_t pos = 0;
        while (pos < static_cast<std::size_t>(leaf->key_count) && leaf->keys[pos] < key) {
            pos++;
        }

        std::string bounded = value.substr(0, kMaxValueSize - 1);

        if (pos < static_cast<std::size_t>(leaf->key_count) && leaf->keys[pos] == key) {
            std::memset(leaf->values[pos], 0, kMaxValueSize);
            std::memcpy(leaf->values[pos], bounded.data(), bounded.size());
            pool_.unpinPage(page_id, true);
            return std::nullopt;
        }

        for (std::size_t i = static_cast<std::size_t>(leaf->key_count); i > pos; --i) {
            leaf->keys[i] = leaf->keys[i - 1];
            std::memcpy(leaf->values[i], leaf->values[i - 1], kMaxValueSize);
        }
        leaf->keys[pos] = key;
        std::memset(leaf->values[pos], 0, kMaxValueSize);
        std::memcpy(leaf->values[pos], bounded.data(), bounded.size());
        leaf->key_count++;

        if (static_cast<std::size_t>(leaf->key_count) <= kMaxLeafKeys) {
            pool_.unpinPage(page_id, true);
            return std::nullopt;
        }
        SplitResult result = splitLeaf(page_id, leaf);
        pool_.unpinPage(page_id, true);
        return result;
    }

    auto* internal = reinterpret_cast<InternalPage*>(raw);
    std::size_t childIndex = 0;
    while (childIndex < static_cast<std::size_t>(internal->key_count) && key >= internal->keys[childIndex]) {
        childIndex++;
    }
    int32_t childId = internal->children[childIndex];
    pool_.unpinPage(page_id, false);

    auto childSplit = insertInto(childId, key, value);
    if (!childSplit) {
        return std::nullopt;
    }

    raw = pool_.fetchPage(page_id);
    internal = reinterpret_cast<InternalPage*>(raw);

    for (std::size_t i = static_cast<std::size_t>(internal->key_count); i > childIndex; --i) {
        internal->keys[i] = internal->keys[i - 1];
        internal->children[i + 1] = internal->children[i];
    }
    internal->keys[childIndex] = childSplit->separator_key;
    internal->children[childIndex + 1] = childSplit->new_right_id;
    internal->key_count++;

    if (static_cast<std::size_t>(internal->key_count) <= kMaxInternalKeys) {
        pool_.unpinPage(page_id, true);
        return std::nullopt;
    }
    SplitResult result = splitInternal(page_id, internal);
    pool_.unpinPage(page_id, true);
    return result;
}

void DiskBPlusTree::insert(int64_t key, const std::string& value) {
    int32_t rootId = getRootPageId();

    if (rootId == kInvalidPageId) {
        auto [newRootId, raw] = pool_.newPage();
        auto* leaf = reinterpret_cast<LeafPage*>(raw);
        leaf->is_leaf = 1;
        leaf->key_count = 1;
        leaf->next_leaf_id = kInvalidPageId;
        leaf->keys[0] = key;
        std::string bounded = value.substr(0, kMaxValueSize - 1);
        std::memset(leaf->values[0], 0, kMaxValueSize);
        std::memcpy(leaf->values[0], bounded.data(), bounded.size());
        pool_.unpinPage(newRootId, true);
        setRootPageId(newRootId);
        return;
    }

    auto split = insertInto(rootId, key, value);
    if (!split) {
        return;
    }

    auto [newRootId, raw] = pool_.newPage();
    auto* newRoot = reinterpret_cast<InternalPage*>(raw);
    newRoot->is_leaf = 0;
    newRoot->key_count = 1;
    newRoot->keys[0] = split->separator_key;
    newRoot->children[0] = rootId;
    newRoot->children[1] = split->new_right_id;
    pool_.unpinPage(newRootId, true);
    setRootPageId(newRootId);
}

std::optional<std::string> DiskBPlusTree::get(int64_t key) const {
    int32_t leafId = findLeafId(key);
    if (leafId == kInvalidPageId) {
        return std::nullopt;
    }

    char* raw = pool_.fetchPage(leafId);
    auto* leaf = reinterpret_cast<LeafPage*>(raw);
    std::size_t pos = 0;
    while (pos < static_cast<std::size_t>(leaf->key_count) && leaf->keys[pos] < key) {
        pos++;
    }

    std::optional<std::string> result;
    if (pos < static_cast<std::size_t>(leaf->key_count) && leaf->keys[pos] == key) {
        result = std::string(leaf->values[pos]);
    }
    pool_.unpinPage(leafId, false);
    return result;
}

std::vector<std::pair<int64_t, std::string>> DiskBPlusTree::rangeScan(int64_t low, int64_t high) const {
    std::vector<std::pair<int64_t, std::string>> result;
    int32_t leafId = findLeafId(low);

    while (leafId != kInvalidPageId) {
        char* raw = pool_.fetchPage(leafId);
        auto* leaf = reinterpret_cast<LeafPage*>(raw);
        int32_t nextId = leaf->next_leaf_id;

        bool stop = false;
        for (std::size_t i = 0; i < static_cast<std::size_t>(leaf->key_count); ++i) {
            if (leaf->keys[i] < low) {
                continue;
            }
            if (leaf->keys[i] > high) {
                stop = true;
                break;
            }
            result.emplace_back(leaf->keys[i], std::string(leaf->values[i]));
        }
        pool_.unpinPage(leafId, false);
        if (stop) {
            break;
        }
        leafId = nextId;
    }
    return result;
}

void DiskBPlusTree::borrowFromLeft(int32_t parentId, std::size_t childIndex) {
    char* parentRaw = pool_.fetchPage(parentId);
    auto* parent = reinterpret_cast<InternalPage*>(parentRaw);
    int32_t childId = parent->children[childIndex];
    int32_t leftId = parent->children[childIndex - 1];

    char* childRaw = pool_.fetchPage(childId);
    char* leftRaw = pool_.fetchPage(leftId);
    bool isLeaf = reinterpret_cast<PageHeader*>(childRaw)->is_leaf != 0;

    if (isLeaf) {
        auto* child = reinterpret_cast<LeafPage*>(childRaw);
        auto* left = reinterpret_cast<LeafPage*>(leftRaw);

        for (int32_t i = child->key_count; i > 0; --i) {
            child->keys[i] = child->keys[i - 1];
            std::memcpy(child->values[i], child->values[i - 1], kMaxValueSize);
        }
        child->keys[0] = left->keys[left->key_count - 1];
        std::memcpy(child->values[0], left->values[left->key_count - 1], kMaxValueSize);
        child->key_count++;
        left->key_count--;

        parent->keys[childIndex - 1] = child->keys[0];
    } else {
        auto* child = reinterpret_cast<InternalPage*>(childRaw);
        auto* left = reinterpret_cast<InternalPage*>(leftRaw);

        for (int32_t i = child->key_count; i > 0; --i) {
            child->keys[i] = child->keys[i - 1];
        }
        for (int32_t i = child->key_count + 1; i > 0; --i) {
            child->children[i] = child->children[i - 1];
        }
        child->keys[0] = parent->keys[childIndex - 1];
        child->children[0] = left->children[left->key_count];
        child->key_count++;

        parent->keys[childIndex - 1] = left->keys[left->key_count - 1];
        left->key_count--;
    }

    pool_.unpinPage(parentId, true);
    pool_.unpinPage(childId, true);
    pool_.unpinPage(leftId, true);
}

void DiskBPlusTree::borrowFromRight(int32_t parentId, std::size_t childIndex) {
    char* parentRaw = pool_.fetchPage(parentId);
    auto* parent = reinterpret_cast<InternalPage*>(parentRaw);
    int32_t childId = parent->children[childIndex];
    int32_t rightId = parent->children[childIndex + 1];

    char* childRaw = pool_.fetchPage(childId);
    char* rightRaw = pool_.fetchPage(rightId);
    bool isLeaf = reinterpret_cast<PageHeader*>(childRaw)->is_leaf != 0;

    if (isLeaf) {
        auto* child = reinterpret_cast<LeafPage*>(childRaw);
        auto* right = reinterpret_cast<LeafPage*>(rightRaw);

        child->keys[child->key_count] = right->keys[0];
        std::memcpy(child->values[child->key_count], right->values[0], kMaxValueSize);
        child->key_count++;

        for (int32_t i = 0; i + 1 < right->key_count; ++i) {
            right->keys[i] = right->keys[i + 1];
            std::memcpy(right->values[i], right->values[i + 1], kMaxValueSize);
        }
        right->key_count--;

        parent->keys[childIndex] = right->keys[0];
    } else {
        auto* child = reinterpret_cast<InternalPage*>(childRaw);
        auto* right = reinterpret_cast<InternalPage*>(rightRaw);

        child->keys[child->key_count] = parent->keys[childIndex];
        child->children[child->key_count + 1] = right->children[0];
        child->key_count++;

        parent->keys[childIndex] = right->keys[0];

        for (int32_t i = 0; i + 1 < right->key_count; ++i) {
            right->keys[i] = right->keys[i + 1];
        }
        for (int32_t i = 0; i < right->key_count; ++i) {
            right->children[i] = right->children[i + 1];
        }
        right->key_count--;
    }

    pool_.unpinPage(parentId, true);
    pool_.unpinPage(childId, true);
    pool_.unpinPage(rightId, true);
}

void DiskBPlusTree::mergeChildren(int32_t parentId, std::size_t leftIndex) {
    char* parentRaw = pool_.fetchPage(parentId);
    auto* parent = reinterpret_cast<InternalPage*>(parentRaw);
    int32_t leftId = parent->children[leftIndex];
    int32_t rightId = parent->children[leftIndex + 1];

    char* leftRaw = pool_.fetchPage(leftId);
    char* rightRaw = pool_.fetchPage(rightId);
    bool isLeaf = reinterpret_cast<PageHeader*>(leftRaw)->is_leaf != 0;

    if (isLeaf) {
        auto* left = reinterpret_cast<LeafPage*>(leftRaw);
        auto* right = reinterpret_cast<LeafPage*>(rightRaw);

        int32_t leftCount = left->key_count;
        for (int32_t i = 0; i < right->key_count; ++i) {
            left->keys[leftCount + i] = right->keys[i];
            std::memcpy(left->values[leftCount + i], right->values[i], kMaxValueSize);
        }
        left->key_count = leftCount + right->key_count;
        left->next_leaf_id = right->next_leaf_id;
    } else {
        auto* left = reinterpret_cast<InternalPage*>(leftRaw);
        auto* right = reinterpret_cast<InternalPage*>(rightRaw);

        int32_t leftCount = left->key_count;
        left->keys[leftCount] = parent->keys[leftIndex];
        for (int32_t i = 0; i < right->key_count; ++i) {
            left->keys[leftCount + 1 + i] = right->keys[i];
        }
        for (int32_t i = 0; i <= right->key_count; ++i) {
            left->children[leftCount + 1 + i] = right->children[i];
        }
        left->key_count = leftCount + 1 + right->key_count;
    }

    for (std::size_t i = leftIndex; i + 1 < static_cast<std::size_t>(parent->key_count); ++i) {
        parent->keys[i] = parent->keys[i + 1];
        parent->children[i + 1] = parent->children[i + 2];
    }
    parent->key_count--;

    pool_.unpinPage(parentId, true);
    pool_.unpinPage(leftId, true);
    pool_.unpinPage(rightId, false);
}

DiskBPlusTree::RemoveOutcome DiskBPlusTree::removeFrom(int32_t page_id, int64_t key) {
    char* raw = pool_.fetchPage(page_id);
    bool isLeaf = reinterpret_cast<PageHeader*>(raw)->is_leaf != 0;

    if (isLeaf) {
        auto* leaf = reinterpret_cast<LeafPage*>(raw);
        std::size_t pos = 0;
        while (pos < static_cast<std::size_t>(leaf->key_count) && leaf->keys[pos] < key) {
            pos++;
        }
        if (pos >= static_cast<std::size_t>(leaf->key_count) || leaf->keys[pos] != key) {
            pool_.unpinPage(page_id, false);
            return RemoveOutcome::NotFound;
        }

        for (std::size_t i = pos; i + 1 < static_cast<std::size_t>(leaf->key_count); ++i) {
            leaf->keys[i] = leaf->keys[i + 1];
            std::memcpy(leaf->values[i], leaf->values[i + 1], kMaxValueSize);
        }
        leaf->key_count--;
        bool underflow = static_cast<std::size_t>(leaf->key_count) < kMinLeafKeys;
        pool_.unpinPage(page_id, true);
        return underflow ? RemoveOutcome::Underflow : RemoveOutcome::Ok;
    }

    auto* internal = reinterpret_cast<InternalPage*>(raw);
    std::size_t childIndex = 0;
    while (childIndex < static_cast<std::size_t>(internal->key_count) && key >= internal->keys[childIndex]) {
        childIndex++;
    }
    int32_t childId = internal->children[childIndex];
    int32_t leftSiblingId = childIndex > 0 ? internal->children[childIndex - 1] : kInvalidPageId;
    int32_t rightSiblingId =
        childIndex + 1 <= static_cast<std::size_t>(internal->key_count) ? internal->children[childIndex + 1] : kInvalidPageId;
    pool_.unpinPage(page_id, false);

    RemoveOutcome childOutcome = removeFrom(childId, key);
    if (childOutcome != RemoveOutcome::Underflow) {
        return childOutcome;
    }

    PageInfo childInfo = inspect(childId);
    std::size_t minForChild = minKeysFor(childInfo.is_leaf);

    bool leftCanLend = leftSiblingId != kInvalidPageId && inspect(leftSiblingId).key_count > minForChild;
    bool rightCanLend = !leftCanLend && rightSiblingId != kInvalidPageId && inspect(rightSiblingId).key_count > minForChild;

    if (leftCanLend) {
        borrowFromLeft(page_id, childIndex);
        return RemoveOutcome::Ok;
    }
    if (rightCanLend) {
        borrowFromRight(page_id, childIndex);
        return RemoveOutcome::Ok;
    }

    if (childIndex > 0) {
        mergeChildren(page_id, childIndex - 1);
    } else {
        mergeChildren(page_id, childIndex);
    }

    PageInfo parentInfo = inspect(page_id);
    return parentInfo.key_count < kMinInternalKeys ? RemoveOutcome::Underflow : RemoveOutcome::Ok;
}

bool DiskBPlusTree::remove(int64_t key) {
    int32_t rootId = getRootPageId();
    if (rootId == kInvalidPageId) {
        return false;
    }

    RemoveOutcome outcome = removeFrom(rootId, key);
    if (outcome == RemoveOutcome::NotFound) {
        return false;
    }

    PageInfo rootInfo = inspect(rootId);
    if (rootInfo.is_leaf && rootInfo.key_count == 0) {
        setRootPageId(kInvalidPageId);
    } else if (!rootInfo.is_leaf && rootInfo.key_count == 0) {
        char* raw = pool_.fetchPage(rootId);
        int32_t onlyChild = reinterpret_cast<InternalPage*>(raw)->children[0];
        pool_.unpinPage(rootId, false);
        setRootPageId(onlyChild);
    }
    return true;
}

}  // namespace storage_engine
