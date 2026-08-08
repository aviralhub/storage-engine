#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace storage_engine {

// small so tests hit splits quickly
constexpr std::size_t kMaxKeysPerNode = 4;

struct Node {
    bool is_leaf;
    explicit Node(bool leaf) : is_leaf(leaf) {}
    virtual ~Node() = default;
};

struct LeafNode : Node {
    std::vector<int64_t> keys;
    std::vector<std::string> values;
    LeafNode* next = nullptr;

    LeafNode() : Node(true) {}
};

// children[i] < keys[i] <= children[i+1]
struct InternalNode : Node {
    std::vector<int64_t> keys;
    std::vector<std::unique_ptr<Node>> children;

    InternalNode() : Node(false) {}
};

class BPlusTree {
public:
    BPlusTree() = default;

    void insert(int64_t key, const std::string& value);
    std::optional<std::string> get(int64_t key) const;
    std::vector<std::pair<int64_t, std::string>> rangeScan(int64_t low, int64_t high) const;
    bool remove(int64_t key);

private:
    std::unique_ptr<Node> root_;
    static constexpr std::size_t kMinKeysPerNode = kMaxKeysPerNode / 2;

    enum class RemoveOutcome { NotFound, Ok, Underflow };

    struct SplitResult {
        int64_t separator_key;
        std::unique_ptr<Node> new_right;
    };

    std::optional<SplitResult> insertInto(Node* node, int64_t key, const std::string& value);
    SplitResult splitLeaf(LeafNode* leaf);
    SplitResult splitInternal(InternalNode* node);
    LeafNode* findLeaf(int64_t key) const;

    RemoveOutcome removeFrom(Node* node, int64_t key);
    static std::size_t keyCount(Node* node);
    void borrowFromLeft(InternalNode* parent, std::size_t childIndex);
    void borrowFromRight(InternalNode* parent, std::size_t childIndex);
    void mergeChildren(InternalNode* parent, std::size_t leftIndex);
};

}  // namespace storage_engine
