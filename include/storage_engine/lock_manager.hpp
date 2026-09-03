#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace storage_engine {

enum class LockMode { Shared, Exclusive };

class LockManager {
public:
    void lock(int64_t txn_id, int64_t resource, LockMode mode);

    void releaseAll(int64_t txn_id);

private:
    struct Holder {
        int64_t txn_id;
        LockMode mode;
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<int64_t, std::vector<Holder>> holders_;
    std::unordered_map<int64_t, std::unordered_set<int64_t>> heldByTxn_;

    bool compatible(int64_t resource, int64_t txn_id, LockMode mode) const;
    void grant(int64_t resource, int64_t txn_id, LockMode mode);
};

}  // namespace storage_engine
