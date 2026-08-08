#include "storage_engine/bplus_tree.hpp"

#include <algorithm>

namespace storage_engine {

BPlusTree::SplitResult BPlusTree::splitLeaf(LeafNode* leaf) {
    std::size_t mid = (leaf->keys.size() + 1) / 2;

    auto right = std::make_unique<LeafNode>();
    right->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
    right->values.assign(leaf->values.begin() + mid, leaf->values.end());
    leaf->keys.resize(mid);
    leaf->values.resize(mid);

    right->next = leaf->next;
    leaf->next = right.get();

    int64_t separator = right->keys.front();
    return SplitResult{separator, std::move(right)};
}

BPlusTree::SplitResult BPlusTree::splitInternal(InternalNode* node) {
    std::size_t mid = node->keys.size() / 2;
    int64_t separator = node->keys[mid];

    auto right = std::make_unique<InternalNode>();
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    for (std::size_t i = mid + 1; i < node->children.size(); ++i) {
        right->children.push_back(std::move(node->children[i]));
    }

    node->keys.resize(mid);
    node->children.resize(mid + 1);

    return SplitResult{separator, std::move(right)};
}

std::optional<BPlusTree::SplitResult> BPlusTree::insertInto(Node* node, int64_t key,
                                                             const std::string& value) {
    if (node->is_leaf) {
        auto* leaf = static_cast<LeafNode*>(node);
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        std::size_t pos = static_cast<std::size_t>(it - leaf->keys.begin());

        if (it != leaf->keys.end() && *it == key) {
            leaf->values[pos] = value;
            return std::nullopt;
        }

        leaf->keys.insert(it, key);
        leaf->values.insert(leaf->values.begin() + static_cast<long>(pos), value);

        if (leaf->keys.size() <= kMaxKeysPerNode) {
            return std::nullopt;
        }
        return splitLeaf(leaf);
    }

    auto* internal = static_cast<InternalNode*>(node);
    std::size_t childIndex = static_cast<std::size_t>(
        std::upper_bound(internal->keys.begin(), internal->keys.end(), key) - internal->keys.begin());

    auto childSplit = insertInto(internal->children[childIndex].get(), key, value);
    if (!childSplit) {
        return std::nullopt;
    }

    internal->keys.insert(internal->keys.begin() + static_cast<long>(childIndex), childSplit->separator_key);
    internal->children.insert(internal->children.begin() + static_cast<long>(childIndex) + 1,
                               std::move(childSplit->new_right));

    if (internal->keys.size() <= kMaxKeysPerNode) {
        return std::nullopt;
    }
    return splitInternal(internal);
}

void BPlusTree::insert(int64_t key, const std::string& value) {
    if (!root_) {
        auto leaf = std::make_unique<LeafNode>();
        leaf->keys.push_back(key);
        leaf->values.push_back(value);
        root_ = std::move(leaf);
        return;
    }

    auto split = insertInto(root_.get(), key, value);
    if (!split) {
        return;
    }

    auto newRoot = std::make_unique<InternalNode>();
    newRoot->keys.push_back(split->separator_key);
    newRoot->children.push_back(std::move(root_));
    newRoot->children.push_back(std::move(split->new_right));
    root_ = std::move(newRoot);
}

LeafNode* BPlusTree::findLeaf(int64_t key) const {
    Node* node = root_.get();
    if (!node) {
        return nullptr;
    }
    while (!node->is_leaf) {
        auto* internal = static_cast<InternalNode*>(node);
        std::size_t idx = static_cast<std::size_t>(
            std::upper_bound(internal->keys.begin(), internal->keys.end(), key) - internal->keys.begin());
        node = internal->children[idx].get();
    }
    return static_cast<LeafNode*>(node);
}

std::optional<std::string> BPlusTree::get(int64_t key) const {
    LeafNode* leaf = findLeaf(key);
    if (!leaf) {
        return std::nullopt;
    }
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        return leaf->values[static_cast<std::size_t>(it - leaf->keys.begin())];
    }
    return std::nullopt;
}

std::vector<std::pair<int64_t, std::string>> BPlusTree::rangeScan(int64_t low, int64_t high) const {
    std::vector<std::pair<int64_t, std::string>> result;
    LeafNode* leaf = findLeaf(low);

    while (leaf) {
        for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] < low) {
                continue;
            }
            if (leaf->keys[i] > high) {
                return result;
            }
            result.emplace_back(leaf->keys[i], leaf->values[i]);
        }
        leaf = leaf->next;
    }
    return result;
}

std::size_t BPlusTree::keyCount(Node* node) {
    return node->is_leaf ? static_cast<LeafNode*>(node)->keys.size()
                          : static_cast<InternalNode*>(node)->keys.size();
}

void BPlusTree::borrowFromLeft(InternalNode* parent, std::size_t childIndex) {
    Node* childNode = parent->children[childIndex].get();
    Node* leftNode = parent->children[childIndex - 1].get();

    if (childNode->is_leaf) {
        auto* child = static_cast<LeafNode*>(childNode);
        auto* left = static_cast<LeafNode*>(leftNode);

        child->keys.insert(child->keys.begin(), left->keys.back());
        child->values.insert(child->values.begin(), left->values.back());
        left->keys.pop_back();
        left->values.pop_back();

        parent->keys[childIndex - 1] = child->keys.front();
    } else {
        auto* child = static_cast<InternalNode*>(childNode);
        auto* left = static_cast<InternalNode*>(leftNode);

        child->keys.insert(child->keys.begin(), parent->keys[childIndex - 1]);
        parent->keys[childIndex - 1] = left->keys.back();
        left->keys.pop_back();

        child->children.insert(child->children.begin(), std::move(left->children.back()));
        left->children.pop_back();
    }
}

void BPlusTree::borrowFromRight(InternalNode* parent, std::size_t childIndex) {
    Node* childNode = parent->children[childIndex].get();
    Node* rightNode = parent->children[childIndex + 1].get();

    if (childNode->is_leaf) {
        auto* child = static_cast<LeafNode*>(childNode);
        auto* right = static_cast<LeafNode*>(rightNode);

        child->keys.push_back(right->keys.front());
        child->values.push_back(right->values.front());
        right->keys.erase(right->keys.begin());
        right->values.erase(right->values.begin());

        parent->keys[childIndex] = right->keys.front();
    } else {
        auto* child = static_cast<InternalNode*>(childNode);
        auto* right = static_cast<InternalNode*>(rightNode);

        child->keys.push_back(parent->keys[childIndex]);
        parent->keys[childIndex] = right->keys.front();
        right->keys.erase(right->keys.begin());

        child->children.push_back(std::move(right->children.front()));
        right->children.erase(right->children.begin());
    }
}

void BPlusTree::mergeChildren(InternalNode* parent, std::size_t leftIndex) {
    Node* leftNode = parent->children[leftIndex].get();
    Node* rightNode = parent->children[leftIndex + 1].get();

    if (leftNode->is_leaf) {
        auto* left = static_cast<LeafNode*>(leftNode);
        auto* right = static_cast<LeafNode*>(rightNode);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        left->values.insert(left->values.end(), right->values.begin(), right->values.end());
        left->next = right->next;
    } else {
        auto* left = static_cast<InternalNode*>(leftNode);
        auto* right = static_cast<InternalNode*>(rightNode);
        left->keys.push_back(parent->keys[leftIndex]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        for (auto& child : right->children) {
            left->children.push_back(std::move(child));
        }
    }

    parent->keys.erase(parent->keys.begin() + static_cast<long>(leftIndex));
    parent->children.erase(parent->children.begin() + static_cast<long>(leftIndex) + 1);
}

BPlusTree::RemoveOutcome BPlusTree::removeFrom(Node* node, int64_t key) {
    if (node->is_leaf) {
        auto* leaf = static_cast<LeafNode*>(node);
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        if (it == leaf->keys.end() || *it != key) {
            return RemoveOutcome::NotFound;
        }

        std::size_t pos = static_cast<std::size_t>(it - leaf->keys.begin());
        leaf->keys.erase(it);
        leaf->values.erase(leaf->values.begin() + static_cast<long>(pos));
        return leaf->keys.size() < kMinKeysPerNode ? RemoveOutcome::Underflow : RemoveOutcome::Ok;
    }

    auto* internal = static_cast<InternalNode*>(node);
    std::size_t childIndex = static_cast<std::size_t>(
        std::upper_bound(internal->keys.begin(), internal->keys.end(), key) - internal->keys.begin());

    RemoveOutcome childOutcome = removeFrom(internal->children[childIndex].get(), key);
    if (childOutcome != RemoveOutcome::Underflow) {
        return childOutcome;
    }

    bool leftCanLend = childIndex > 0 &&
        keyCount(internal->children[childIndex - 1].get()) > kMinKeysPerNode;
    bool rightCanLend = childIndex + 1 < internal->children.size() &&
        keyCount(internal->children[childIndex + 1].get()) > kMinKeysPerNode;

    if (leftCanLend) {
        borrowFromLeft(internal, childIndex);
        return RemoveOutcome::Ok;
    }
    if (rightCanLend) {
        borrowFromRight(internal, childIndex);
        return RemoveOutcome::Ok;
    }

    if (childIndex > 0) {
        mergeChildren(internal, childIndex - 1);
    } else {
        mergeChildren(internal, childIndex);
    }
    return internal->keys.size() < kMinKeysPerNode ? RemoveOutcome::Underflow : RemoveOutcome::Ok;
}

bool BPlusTree::remove(int64_t key) {
    if (!root_) {
        return false;
    }

    RemoveOutcome outcome = removeFrom(root_.get(), key);
    if (outcome == RemoveOutcome::NotFound) {
        return false;
    }

    if (root_->is_leaf) {
        if (static_cast<LeafNode*>(root_.get())->keys.empty()) {
            root_.reset();
        }
    } else {
        auto* internal = static_cast<InternalNode*>(root_.get());
        if (internal->keys.empty()) {
            root_ = std::move(internal->children[0]);
        }
    }
    return true;
}

}  // namespace storage_engine
