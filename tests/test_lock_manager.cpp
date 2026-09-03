#include "storage_engine/lock_manager.hpp"

#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

TEST_CASE("two shared locks on the same resource are both granted immediately", "[lock_manager]") {
    LockManager lm;
    lm.lock(1, 100, LockMode::Shared);
    lm.lock(2, 100, LockMode::Shared);
    lm.releaseAll(1);
    lm.releaseAll(2);
}

TEST_CASE("an exclusive lock blocks a second transaction until released", "[lock_manager]") {
    LockManager lm;
    lm.lock(1, 100, LockMode::Exclusive);

    std::atomic<bool> gotLock{false};
    std::thread t([&] {
        lm.lock(2, 100, LockMode::Exclusive);
        gotLock = true;
        lm.releaseAll(2);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(gotLock.load());

    lm.releaseAll(1);
    t.join();
    REQUIRE(gotLock.load());
}

TEST_CASE("a transaction can upgrade its own shared lock to exclusive without blocking on itself",
          "[lock_manager]") {
    LockManager lm;
    lm.lock(1, 100, LockMode::Shared);
    lm.lock(1, 100, LockMode::Exclusive);
    lm.releaseAll(1);
}

TEST_CASE("an upgrade still blocks while another transaction also holds the shared lock",
          "[lock_manager]") {
    LockManager lm;
    lm.lock(1, 100, LockMode::Shared);
    lm.lock(2, 100, LockMode::Shared);

    std::atomic<bool> upgraded{false};
    std::thread t([&] {
        lm.lock(1, 100, LockMode::Exclusive);
        upgraded = true;
        lm.releaseAll(1);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(upgraded.load());

    lm.releaseAll(2);
    t.join();
    REQUIRE(upgraded.load());
}

TEST_CASE("requesting shared after already holding exclusive does not weaken the lock", "[lock_manager]") {
    LockManager lm;
    lm.lock(1, 100, LockMode::Exclusive);
    lm.lock(1, 100, LockMode::Shared);

    std::atomic<bool> gotLock{false};
    std::thread t([&] {
        lm.lock(2, 100, LockMode::Shared);
        gotLock = true;
        lm.releaseAll(2);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(gotLock.load());

    lm.releaseAll(1);
    t.join();
    REQUIRE(gotLock.load());
}

TEST_CASE("releaseAll on an unknown transaction is a harmless no-op", "[lock_manager]") {
    LockManager lm;
    lm.releaseAll(999);
}

TEST_CASE("many threads incrementing the same counter under exclusive locks lose no updates",
          "[lock_manager]") {
    LockManager lm;
    int64_t counter = 0;
    constexpr int kThreads = 8;
    constexpr int kIncrementsPerThread = 500;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kIncrementsPerThread; ++i) {
                int64_t txnId = t * 100000 + i;
                lm.lock(txnId, 42, LockMode::Exclusive);
                counter++;
                lm.releaseAll(txnId);
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(counter == kThreads * kIncrementsPerThread);
}
