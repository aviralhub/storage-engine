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

}  // namespace storage_engine
