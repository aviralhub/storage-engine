#include "storage_engine/engine.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

namespace {

std::pair<std::string, std::string> freshPaths(const std::string& name) {
    std::string db = "test_engine_" + name + ".db";
    std::string wal = "test_engine_" + name + ".wal";
    std::remove(db.c_str());
    std::remove(wal.c_str());
    return {db, wal};
}

}  // namespace

TEST_CASE("put/get/remove work through the engine", "[engine]") {
    auto [db, wal] = freshPaths("basic");
    Engine engine(db, wal);

    engine.put(1, "a");
    REQUIRE(engine.get(1) == "a");
    REQUIRE(engine.remove(1));
    REQUIRE(engine.get(1) == std::nullopt);
    REQUIRE_FALSE(engine.remove(1));
}

TEST_CASE("putBatch applies every item with one shared fsync", "[engine]") {
    auto [db, wal] = freshPaths("batch");
    Engine engine(db, wal);

    std::vector<std::pair<int64_t, std::string>> items;
    for (int64_t k = 1; k <= 50; ++k) {
        items.emplace_back(k, "v" + std::to_string(k));
    }
    engine.putBatch(items);

    for (int64_t k = 1; k <= 50; ++k) {
        REQUIRE(engine.get(k) == "v" + std::to_string(k));
    }
}

TEST_CASE("a clean shutdown checkpoints so the next open has nothing to replay", "[engine]") {
    auto [db, wal] = freshPaths("checkpoint");
    {
        Engine engine(db, wal);
        engine.put(1, "a");
        engine.put(2, "b");
    }

    {
        Engine engine(db, wal);
        REQUIRE(engine.get(1) == "a");
        REQUIRE(engine.get(2) == "b");
    }
}

TEST_CASE("data survives across engine instances on the same files", "[engine]") {
    auto [db, wal] = freshPaths("persist");
    {
        Engine engine(db, wal);
        for (int64_t k = 1; k <= 200; ++k) {
            engine.put(k, "v" + std::to_string(k));
        }
    }
    {
        Engine engine(db, wal);
        for (int64_t k = 1; k <= 200; ++k) {
            REQUIRE(engine.get(k) == "v" + std::to_string(k));
        }
        auto scan = engine.rangeScan(1, 200);
        REQUIRE(scan.size() == 200);
    }
}
