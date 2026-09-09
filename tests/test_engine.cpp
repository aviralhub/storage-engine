#include "storage_engine/engine.hpp"

#include <thread>

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

TEST_CASE("applyBatch commits a mix of puts and deletes together", "[engine]") {
    auto [db, wal] = freshPaths("mixedbatch");
    Engine engine(db, wal);

    engine.put(1, "old");
    engine.applyBatch({
        {WalRecordType::Put, 2, "new"},
        {WalRecordType::Delete, 1, ""},
        {WalRecordType::Put, 3, "three"},
    });

    REQUIRE(engine.get(1) == std::nullopt);
    REQUIRE(engine.get(2) == "new");
    REQUIRE(engine.get(3) == "three");
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

TEST_CASE("a committed transaction's writes are visible afterward", "[engine][txn]") {
    auto [db, wal] = freshPaths("txn_commit");
    Engine engine(db, wal);

    int64_t txn = engine.beginTxn();
    engine.txnPut(txn, 1, "a");
    engine.txnPut(txn, 2, "b");
    REQUIRE(engine.txnGet(txn, 1) == std::nullopt);  // no read-your-own-writes
    engine.commitTxn(txn);

    REQUIRE(engine.get(1) == "a");
    REQUIRE(engine.get(2) == "b");
}

TEST_CASE("an aborted transaction's writes never apply", "[engine][txn]") {
    auto [db, wal] = freshPaths("txn_abort");
    Engine engine(db, wal);

    engine.put(1, "original");
    int64_t txn = engine.beginTxn();
    engine.txnPut(txn, 1, "changed");
    engine.txnPut(txn, 2, "new");
    engine.abortTxn(txn);

    REQUIRE(engine.get(1) == "original");
    REQUIRE(engine.get(2) == std::nullopt);
}

TEST_CASE("concurrent transactions writing disjoint keys all land correctly", "[engine][txn]") {
    auto [db, wal] = freshPaths("txn_concurrent");
    Engine engine(db, wal);

    constexpr int kThreads = 6;
    constexpr int kOpsPerThread = 200;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kOpsPerThread; ++i) {
                int64_t key = t * 100000 + i;
                int64_t txn = engine.beginTxn();
                engine.txnPut(txn, key, "v" + std::to_string(key));
                engine.commitTxn(txn);
            }
        });
    }
    for (auto& th : threads) th.join();

    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kOpsPerThread; ++i) {
            int64_t key = t * 100000 + i;
            REQUIRE(engine.get(key) == "v" + std::to_string(key));
        }
    }
}

TEST_CASE("concurrent read-modify-write with deadlock retry converges to the right total",
          "[engine][txn][deadlock]") {
    auto [db, wal] = freshPaths("txn_retry_counter");
    Engine engine(db, wal);
    engine.put(100, "0");

    constexpr int kThreads = 6;
    constexpr int kIncrementsPerThread = 50;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kIncrementsPerThread; ++i) {
                while (true) {
                    try {
                        int64_t txn = engine.beginTxn();
                        auto current = engine.txnGet(txn, 100);
                        int value = std::stoi(*current);
                        engine.txnPut(txn, 100, std::to_string(value + 1));
                        engine.commitTxn(txn);
                        break;
                    } catch (const TransactionAborted&) {
                    }
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(engine.get(100) == std::to_string(kThreads * kIncrementsPerThread));
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
