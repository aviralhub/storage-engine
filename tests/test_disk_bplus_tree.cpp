#include "storage_engine/disk_bplus_tree.hpp"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

namespace {

std::string freshDbPath(const std::string& name) {
    std::string path = "test_dbt_" + name + ".db";
    std::remove(path.c_str());
    return path;
}

}  // namespace

TEST_CASE("empty disk tree has no keys", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("empty"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);
    REQUIRE(tree.get(1) == std::nullopt);
}

TEST_CASE("a single inserted key can be read back from disk", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("single"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    tree.insert(42, "answer");
    REQUIRE(tree.get(42) == "answer");
    REQUIRE(tree.get(43) == std::nullopt);
}

TEST_CASE("inserting an existing key overwrites its value", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("overwrite"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    tree.insert(1, "first");
    tree.insert(1, "second");
    REQUIRE(tree.get(1) == "second");
}

TEST_CASE("enough inserts force leaf pages to split", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("leafsplit"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    for (int64_t key = 1; key <= 200; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key = 1; key <= 200; ++key) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }
    REQUIRE(tree.get(0) == std::nullopt);
    REQUIRE(tree.get(201) == std::nullopt);
}

TEST_CASE("insertions out of order still land in the right place on disk", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("outoforder"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    std::vector<int64_t> keys;
    for (int64_t k = 500; k >= 1; k -= 7) keys.push_back(k);
    for (int64_t k = 2; k <= 500; k += 7) keys.push_back(k);

    for (int64_t key : keys) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key : keys) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }
}

TEST_CASE("range scan across several leaf pages returns keys in order", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("rangescan"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    for (int64_t key = 1; key <= 300; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }

    auto result = tree.rangeScan(50, 120);
    REQUIRE(result.size() == 71);
    for (std::size_t i = 0; i + 1 < result.size(); ++i) {
        REQUIRE(result[i].first < result[i + 1].first);
    }
    REQUIRE(result.front().first == 50);
    REQUIRE(result.back().first == 120);
}

TEST_CASE("removing keys forces borrow/merge across pages and the tree stays correct", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("remove"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    for (int64_t key = 1; key <= 300; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }

    std::vector<int64_t> removed;
    for (int64_t key = 1; key <= 300; key += 3) {
        REQUIRE(tree.remove(key));
        removed.push_back(key);
    }

    for (int64_t key = 1; key <= 300; ++key) {
        bool wasRemoved = std::find(removed.begin(), removed.end(), key) != removed.end();
        if (wasRemoved) {
            REQUIRE(tree.get(key) == std::nullopt);
        } else {
            REQUIRE(tree.get(key) == "v" + std::to_string(key));
        }
    }
}

TEST_CASE("removing every key empties a disk tree completely", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("removeall"));
    BufferPool pool(dm, 64);
    DiskBPlusTree tree(pool);

    for (int64_t key = 1; key <= 150; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key = 1; key <= 150; ++key) {
        REQUIRE(tree.remove(key));
    }
    for (int64_t key = 1; key <= 150; ++key) {
        REQUIRE(tree.get(key) == std::nullopt);
    }
    REQUIRE(tree.rangeScan(1, 150).empty());

    tree.insert(7, "fresh");
    REQUIRE(tree.get(7) == "fresh");
}

TEST_CASE("enough inserts force an internal page to split too, not just leaves", "[disk_bplus_tree]") {
    DiskManager dm(freshDbPath("internalsplit"));
    BufferPool pool(dm, 512);
    DiskBPlusTree tree(pool);

    constexpr int64_t kCount = 10000;
    for (int64_t key = 0; key < kCount; ++key) {
        tree.insert(key, "v" + std::to_string(key));
    }
    for (int64_t key = 0; key < kCount; ++key) {
        REQUIRE(tree.get(key) == "v" + std::to_string(key));
    }

    auto scan = tree.rangeScan(0, kCount - 1);
    REQUIRE(scan.size() == static_cast<std::size_t>(kCount));
    for (std::size_t i = 0; i + 1 < scan.size(); ++i) {
        REQUIRE(scan[i].first + 1 == scan[i + 1].first);
    }
}

TEST_CASE("data survives closing and reopening the same file", "[disk_bplus_tree]") {
    std::string path = freshDbPath("persist");
    {
        DiskManager dm(path);
        BufferPool pool(dm, 64);
        DiskBPlusTree tree(pool);
        for (int64_t key = 1; key <= 100; ++key) {
            tree.insert(key, "v" + std::to_string(key));
        }
        pool.flushAll();
    }
    {
        DiskManager dm(path);
        BufferPool pool(dm, 64);
        DiskBPlusTree tree(pool);
        for (int64_t key = 1; key <= 100; ++key) {
            REQUIRE(tree.get(key) == "v" + std::to_string(key));
        }
        auto scan = tree.rangeScan(1, 100);
        REQUIRE(scan.size() == 100);
    }
}
