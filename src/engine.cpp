#include "storage_engine/engine.hpp"

namespace storage_engine {

Engine::Engine(const std::string& db_path, const std::string& wal_path, std::size_t pool_size)
    : disk_manager_(db_path), pool_(disk_manager_, pool_size), tree_(pool_), wal_(wal_path) {
    recover();
}

Engine::~Engine() {
    checkpoint();
}

void Engine::recover() {
    wal_.replay([this](const WalRecord& rec) {
        if (rec.type == WalRecordType::Put) {
            tree_.insert(rec.key, rec.value);
        } else {
            tree_.remove(rec.key);
        }
    });
    checkpoint();
}

void Engine::put(int64_t key, const std::string& value) {
    wal_.append(WalRecordType::Put, key, value);
    tree_.insert(key, value);
}

void Engine::putBatch(const std::vector<std::pair<int64_t, std::string>>& items) {
    for (const auto& [key, value] : items) {
        wal_.append(WalRecordType::Put, key, value, /*sync=*/false);
    }
    wal_.flush();
    for (const auto& [key, value] : items) {
        tree_.insert(key, value);
    }
}

void Engine::applyBatch(const std::vector<BatchOp>& ops) {
    if (ops.empty()) {
        return;
    }
    for (const auto& op : ops) {
        wal_.append(op.type, op.key, op.value, /*sync=*/false);
    }
    wal_.flush();
    for (const auto& op : ops) {
        if (op.type == WalRecordType::Put) {
            tree_.insert(op.key, op.value);
        } else {
            tree_.remove(op.key);
        }
    }
}

bool Engine::remove(int64_t key) {
    if (!tree_.get(key).has_value()) {
        return false;
    }
    wal_.append(WalRecordType::Delete, key, "");
    return tree_.remove(key);
}

std::optional<std::string> Engine::get(int64_t key) const {
    return tree_.get(key);
}

std::vector<std::pair<int64_t, std::string>> Engine::rangeScan(int64_t low, int64_t high) const {
    return tree_.rangeScan(low, high);
}

void Engine::checkpoint() {
    pool_.flushAll();
    disk_manager_.sync();
    wal_.reset();
}

int64_t Engine::beginTxn() {
    int64_t id = nextTxnId_.fetch_add(1);
    std::lock_guard<std::mutex> guard(txnStateMutex_);
    txnPending_[id] = {};
    return id;
}

std::optional<std::string> Engine::txnGet(int64_t txn_id, int64_t key) {
    lockManager_.lock(txn_id, key, LockMode::Shared);
    std::lock_guard<std::mutex> guard(treeMutex_);
    return tree_.get(key);
}

void Engine::txnPut(int64_t txn_id, int64_t key, const std::string& value) {
    lockManager_.lock(txn_id, key, LockMode::Exclusive);
    std::lock_guard<std::mutex> guard(txnStateMutex_);
    txnPending_[txn_id].push_back({WalRecordType::Put, key, value});
}

void Engine::txnRemove(int64_t txn_id, int64_t key) {
    lockManager_.lock(txn_id, key, LockMode::Exclusive);
    std::lock_guard<std::mutex> guard(txnStateMutex_);
    txnPending_[txn_id].push_back({WalRecordType::Delete, key, ""});
}

void Engine::commitTxn(int64_t txn_id) {
    std::vector<BatchOp> ops;
    {
        std::lock_guard<std::mutex> guard(txnStateMutex_);
        auto it = txnPending_.find(txn_id);
        if (it != txnPending_.end()) {
            ops = std::move(it->second);
            txnPending_.erase(it);
        }
    }
    {
        std::lock_guard<std::mutex> guard(treeMutex_);
        applyBatch(ops);
    }
    lockManager_.releaseAll(txn_id);
}

void Engine::abortTxn(int64_t txn_id) {
    {
        std::lock_guard<std::mutex> guard(txnStateMutex_);
        txnPending_.erase(txn_id);
    }
    lockManager_.releaseAll(txn_id);
}

}  // namespace storage_engine
