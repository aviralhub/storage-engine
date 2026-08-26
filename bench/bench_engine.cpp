// run in WSL2 against ext4 (/root/bench_scratch), /tmp is tmpfs

#include "storage_engine/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace storage_engine;
using Clock = std::chrono::steady_clock;

namespace {

constexpr uint32_t kSeed = 20260912;
constexpr const char* kDbPath = "/root/bench_scratch/storage_engine_bench.db";
constexpr const char* kWalPath = "/root/bench_scratch/storage_engine_bench.wal";

void reset(const char* path) {
    std::remove(path);
}

int64_t percentile(std::vector<int64_t>& sorted, double fraction) {
    if (sorted.empty()) return 0;
    std::size_t idx = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size()));
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

void report(const char* label, std::vector<int64_t> samples_us, double seconds) {
    std::sort(samples_us.begin(), samples_us.end());
    double throughput = static_cast<double>(samples_us.size()) / seconds;
    std::printf("%-28s ops=%7zu  %8.0f ops/sec   p50=%6lldus  p99=%7lldus  max=%8lldus\n", label,
                samples_us.size(), throughput, static_cast<long long>(percentile(samples_us, 0.50)),
                static_cast<long long>(percentile(samples_us, 0.99)),
                static_cast<long long>(samples_us.empty() ? 0 : samples_us.back()));
}

std::vector<int64_t> makeShuffledKeys(int64_t n, std::mt19937& rng) {
    std::vector<int64_t> keys(static_cast<std::size_t>(n));
    for (int64_t i = 0; i < n; ++i) keys[static_cast<std::size_t>(i)] = i;
    std::shuffle(keys.begin(), keys.end(), rng);
    return keys;
}

void benchPerCommitInsert(const char* label, const std::vector<int64_t>& keys) {
    reset(kDbPath);
    reset(kWalPath);
    Engine engine(kDbPath, kWalPath);

    std::vector<int64_t> latencies_us;
    latencies_us.reserve(keys.size());

    auto start = Clock::now();
    for (int64_t k : keys) {
        auto t0 = Clock::now();
        engine.put(k, "v" + std::to_string(k));
        auto t1 = Clock::now();
        latencies_us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }
    auto end = Clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    report(label, std::move(latencies_us), seconds);
}

void benchGroupCommitInsert(const char* label, const std::vector<int64_t>& keys, std::size_t batchSize) {
    reset(kDbPath);
    reset(kWalPath);
    Engine engine(kDbPath, kWalPath);

    std::vector<int64_t> latencies_us;
    latencies_us.reserve(keys.size() / batchSize + 1);

    auto start = Clock::now();
    std::vector<std::pair<int64_t, std::string>> batch;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        batch.emplace_back(keys[i], "v" + std::to_string(keys[i]));
        if (batch.size() == batchSize || i + 1 == keys.size()) {
            auto t0 = Clock::now();
            engine.putBatch(batch);
            auto t1 = Clock::now();
            latencies_us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
            batch.clear();
        }
    }
    auto end = Clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::printf("%-28s batches=%4zu  %8.0f effective ops/sec (batch size %zu)\n", label, latencies_us.size(),
                static_cast<double>(keys.size()) / seconds, batchSize);
}

void benchPointLookup(int64_t datasetSize, int64_t lookups, std::mt19937& rng) {
    reset(kDbPath);
    reset(kWalPath);
    Engine engine(kDbPath, kWalPath);

    std::vector<std::pair<int64_t, std::string>> items;
    for (int64_t k = 0; k < datasetSize; ++k) items.emplace_back(k, "v" + std::to_string(k));
    engine.putBatch(items);

    std::uniform_int_distribution<int64_t> dist(0, datasetSize - 1);
    std::vector<int64_t> latencies_us;
    latencies_us.reserve(static_cast<std::size_t>(lookups));

    auto start = Clock::now();
    for (int64_t i = 0; i < lookups; ++i) {
        int64_t key = dist(rng);
        auto t0 = Clock::now();
        auto v = engine.get(key);
        auto t1 = Clock::now();
        (void)v;
        latencies_us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }
    auto end = Clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    report("point lookup (random)", std::move(latencies_us), seconds);
}

void benchRangeScan(int64_t datasetSize, const std::vector<int64_t>& rangeSizes) {
    reset(kDbPath);
    reset(kWalPath);
    Engine engine(kDbPath, kWalPath);

    std::vector<std::pair<int64_t, std::string>> items;
    for (int64_t k = 0; k < datasetSize; ++k) items.emplace_back(k, "v" + std::to_string(k));
    engine.putBatch(items);

    for (int64_t rangeSize : rangeSizes) {
        int64_t low = datasetSize / 2;
        int64_t high = low + rangeSize - 1;

        auto start = Clock::now();
        auto result = engine.rangeScan(low, high);
        auto end = Clock::now();
        double seconds = std::chrono::duration<double>(end - start).count();
        double elementsPerSec = static_cast<double>(result.size()) / seconds;

        std::printf("range scan of %-10lld elements: %8.3f ms total, %10.0f elements/sec\n",
                    static_cast<long long>(rangeSize), seconds * 1000.0, elementsPerSec);
    }
}

void benchRecoveryTime(const std::vector<int64_t>& walSizes) {
    for (int64_t walSize : walSizes) {
        reset(kDbPath);
        reset(kWalPath);
        {
            WriteAheadLog wal(kWalPath);
            for (int64_t k = 0; k < walSize; ++k) {
                wal.append(WalRecordType::Put, k, "v" + std::to_string(k));
            }
        }

        auto start = Clock::now();
        { Engine engine(kDbPath, kWalPath); }
        auto end = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::printf("recovery with %6lld uncheckpointed WAL records: %8.2f ms\n",
                    static_cast<long long>(walSize), ms);
    }
}

}  // namespace

int main() {
    std::mt19937 rng(kSeed);

    std::printf("=== Insert throughput/latency (per-commit fsync vs group commit) ===\n");
    constexpr int64_t kInsertCount = 3000;
    {
        std::vector<int64_t> sequential(static_cast<std::size_t>(kInsertCount));
        for (int64_t i = 0; i < kInsertCount; ++i) sequential[static_cast<std::size_t>(i)] = i;
        benchPerCommitInsert("sequential insert (fsync/op)", sequential);
    }
    {
        auto shuffled = makeShuffledKeys(kInsertCount, rng);
        benchPerCommitInsert("random insert (fsync/op)", shuffled);
    }
    {
        auto shuffled = makeShuffledKeys(kInsertCount, rng);
        benchGroupCommitInsert("random insert (batch=100)", shuffled, 100);
    }

    std::printf("\n=== Point lookup / range scan (no WAL involved - reads only) ===\n");
    benchPointLookup(50000, 20000, rng);
    benchRangeScan(50000, {10, 100, 1000, 10000});

    std::printf("\n=== Recovery time vs uncheckpointed WAL size ===\n");
    benchRecoveryTime({100, 1000, 10000});

    return 0;
}
