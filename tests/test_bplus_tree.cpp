#include "storage_engine/bplus_tree.hpp"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

TEST_CASE("empty tree has no keys", "[bplus_tree]") {
    BPlusTree tree;
    REQUIRE(tree.get(1) == std::nullopt);
}

TEST_CASE("a single inserted key can be read back", "[bplus_tree]") {
    BPlusTree tree;
    tree.insert(42, "answer");
    REQUIRE(tree.get(42) == "answer");
    REQUIRE(tree.get(43) == std::nullopt);
}

TEST_CASE("inserting an existing key overwrites its value instead of duplicating it", "[bplus_tree]") {
    BPlusTree tree;
    tree.insert(1, "first");
    tree.insert(1, "second");
    REQUIRE(tree.get(1) == "second");
}

TEST_CASE("enough insertions force a leaf split and lookups still work on both sides", "[bplus_tree]") {
    BPlusTree tree;
    for (int64_t key = 1; key <= 5; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key = 1; key <= 5; ++key) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }
}

TEST_CASE("enough insertions force the root itself to split, growing the tree by a level", "[bplus_tree]") {
    BPlusTree tree;
    for (int64_t key = 1; key <= 40; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key = 1; key <= 40; ++key) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }
    REQUIRE(tree.get(0) == std::nullopt);
    REQUIRE(tree.get(41) == std::nullopt);
}

TEST_CASE("insertions out of order still land in the right place", "[bplus_tree]") {
    BPlusTree tree;
    std::vector<int64_t> keys = {50, 10, 30, 20, 40, 5, 45, 15, 35, 25};
    for (int64_t key : keys) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key : keys) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }
}

TEST_CASE("range scan returns keys in order across a leaf split", "[bplus_tree]") {
    BPlusTree tree;
    for (int64_t key = 1; key <= 20; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }

    auto result = tree.rangeScan(5, 12);
    REQUIRE(result.size() == 8);
    for (std::size_t i = 0; i < result.size(); ++i) {
        REQUIRE(result[i].first == static_cast<int64_t>(5 + i));
        REQUIRE(result[i].second == "v" + std::to_string(5 + i));
    }
}

TEST_CASE("range scan with no matching keys returns empty", "[bplus_tree]") {
    BPlusTree tree;
    tree.insert(1, "a");
    tree.insert(2, "b");
    REQUIRE(tree.rangeScan(100, 200).empty());
}

TEST_CASE("removing from an empty tree returns false", "[bplus_tree][remove]") {
    BPlusTree tree;
    REQUIRE_FALSE(tree.remove(1));
}

TEST_CASE("removing the only key leaves the tree empty", "[bplus_tree][remove]") {
    BPlusTree tree;
    tree.insert(1, "a");
    REQUIRE(tree.remove(1));
    REQUIRE(tree.get(1) == std::nullopt);
    tree.insert(2, "b");
    REQUIRE(tree.get(2) == "b");
}

TEST_CASE("removing a key that was never inserted returns false and changes nothing", "[bplus_tree][remove]") {
    BPlusTree tree;
    tree.insert(1, "a");
    tree.insert(2, "b");
    REQUIRE_FALSE(tree.remove(99));
    REQUIRE(tree.get(1) == "a");
    REQUIRE(tree.get(2) == "b");
}

TEST_CASE("removing one key from a split leaf leaves the rest reachable", "[bplus_tree][remove]") {
    BPlusTree tree;
    for (int64_t key = 1; key <= 5; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    REQUIRE(tree.remove(3));
    REQUIRE(tree.get(3) == std::nullopt);
    for (int64_t key : {1, 2, 4, 5}) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }
}

TEST_CASE("removing enough keys forces borrowing or merging and the tree stays correct", "[bplus_tree][remove]") {
    BPlusTree tree;
    for (int64_t key = 1; key <= 40; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }

    std::vector<int64_t> removed;
    for (int64_t key = 1; key <= 40; key += 3) {
        REQUIRE(tree.remove(key));
        removed.push_back(key);
    }

    for (int64_t key = 1; key <= 40; ++key) {
        bool wasRemoved = std::find(removed.begin(), removed.end(), key) != removed.end();
        if (wasRemoved) {
            REQUIRE(tree.get(key) == std::nullopt);
        } else {
            REQUIRE(tree.get(key) == "v" + std::to_string(key));
        }
    }

    auto scan = tree.rangeScan(1, 40);
    REQUIRE(scan.size() == 40 - removed.size());
    for (std::size_t i = 0; i + 1 < scan.size(); ++i) {
        REQUIRE(scan[i].first < scan[i + 1].first);
    }
}

TEST_CASE("removing every key one by one empties the tree completely", "[bplus_tree][remove]") {
    BPlusTree tree;
    for (int64_t key = 1; key <= 50; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key = 1; key <= 50; ++key) {
        REQUIRE(tree.remove(key));
    }
    for (int64_t key = 1; key <= 50; ++key) {
        REQUIRE(tree.get(key) == std::nullopt);
    }
    REQUIRE(tree.rangeScan(1, 50).empty());

    tree.insert(7, "fresh");
    REQUIRE(tree.get(7) == "fresh");
}
