#include "storage_engine/lock_manager.hpp"

#include <algorithm>

namespace storage_engine {

bool LockManager::compatible(int64_t resource, int64_t txn_id, LockMode mode) const {
    auto it = holders_.find(resource);
    if (it == holders_.end()) {
        return true;
    }
    for (const auto& h : it->second) {
        if (h.txn_id == txn_id) {
            continue;
        }
        if (mode == LockMode::Exclusive || h.mode == LockMode::Exclusive) {
            return false;
        }
    }
    return true;
}

void LockManager::grant(int64_t resource, int64_t txn_id, LockMode mode) {
    auto& vec = holders_[resource];
    for (auto& h : vec) {
        if (h.txn_id == txn_id) {
            if (mode == LockMode::Exclusive) {
                h.mode = LockMode::Exclusive;
            }
            heldByTxn_[txn_id].insert(resource);
            return;
        }
    }
    vec.push_back({txn_id, mode});
    heldByTxn_[txn_id].insert(resource);
}

void LockManager::lock(int64_t txn_id, int64_t resource, LockMode mode) {
    std::unique_lock<std::mutex> guard(mutex_);
    cv_.wait(guard, [&] { return compatible(resource, txn_id, mode); });
    grant(resource, txn_id, mode);
}

void LockManager::releaseAll(int64_t txn_id) {
    std::unique_lock<std::mutex> guard(mutex_);
    auto it = heldByTxn_.find(txn_id);
    if (it == heldByTxn_.end()) {
        return;
    }
    for (int64_t resource : it->second) {
        auto vecIt = holders_.find(resource);
        if (vecIt == holders_.end()) {
            continue;
        }
        auto& vec = vecIt->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                  [&](const Holder& h) { return h.txn_id == txn_id; }),
                  vec.end());
        if (vec.empty()) {
            holders_.erase(vecIt);
        }
    }
    heldByTxn_.erase(it);
    guard.unlock();
    cv_.notify_all();
}

}  // namespace storage_engine
