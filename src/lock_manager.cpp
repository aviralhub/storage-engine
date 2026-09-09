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

bool LockManager::wouldDeadlock(int64_t requester, int64_t resource) const {
    auto seed = holders_.find(resource);
    if (seed == holders_.end()) {
        return false;
    }

    std::unordered_set<int64_t> visited;
    std::vector<int64_t> stack;
    for (const auto& h : seed->second) {
        if (h.txn_id != requester) {
            stack.push_back(h.txn_id);
        }
    }

    while (!stack.empty()) {
        int64_t current = stack.back();
        stack.pop_back();
        if (current == requester) {
            return true;
        }
        if (!visited.insert(current).second) {
            continue;
        }

        auto waitIt = waitingOnResource_.find(current);
        if (waitIt == waitingOnResource_.end()) {
            continue;
        }
        auto holdersIt = holders_.find(waitIt->second);
        if (holdersIt == holders_.end()) {
            continue;
        }
        for (const auto& h : holdersIt->second) {
            stack.push_back(h.txn_id);
        }
    }
    return false;
}

bool LockManager::lock(int64_t txn_id, int64_t resource, LockMode mode) {
    std::unique_lock<std::mutex> guard(mutex_);

    if (compatible(resource, txn_id, mode)) {
        grant(resource, txn_id, mode);
        return true;
    }

    if (wouldDeadlock(txn_id, resource)) {
        return false;
    }

    waitingOnResource_[txn_id] = resource;
    cv_.wait(guard, [&] { return compatible(resource, txn_id, mode); });
    waitingOnResource_.erase(txn_id);

    grant(resource, txn_id, mode);
    return true;
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
