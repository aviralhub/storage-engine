#pragma once

#include "storage_engine/buffer_pool.hpp"
#include "storage_engine/disk_bplus_tree.hpp"
#include "storage_engine/disk_manager.hpp"
#include "storage_engine/lock_manager.hpp"
#include "storage_engine/wal.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace storage_engine {

class Engine {
public:
    Engine(const std::string& db_path, const std::string& wal_path, std::size_t pool_size = 128);
    ~Engine();

    void put(int64_t key, const std::string& value);
    void putBatch(const std::vector<std::pair<int64_t, std::string>>& items);
    bool remove(int64_t key);

    struct BatchOp {
        WalRecordType type;
        int64_t key;
        std::string value;
    };
    void applyBatch(const std::vector<BatchOp>& ops);
    std::optional<std::string> get(int64_t key) const;
    std::vector<std::pair<int64_t, std::string>> rangeScan(int64_t low, int64_t high) const;

    void checkpoint();

    // thread-safe; don't mix with the calls above across threads
    int64_t beginTxn();
    std::optional<std::string> txnGet(int64_t txn_id, int64_t key);
    void txnPut(int64_t txn_id, int64_t key, const std::string& value);
    void txnRemove(int64_t txn_id, int64_t key);
    void commitTxn(int64_t txn_id);
    void abortTxn(int64_t txn_id);

    // for the crash test
    WriteAheadLog& wal() { return wal_; }

private:
    DiskManager disk_manager_;
    BufferPool pool_;
    DiskBPlusTree tree_;
    WriteAheadLog wal_;

    std::mutex treeMutex_;
    LockManager lockManager_;
    std::mutex txnStateMutex_;
    std::unordered_map<int64_t, std::vector<BatchOp>> txnPending_;
    std::atomic<int64_t> nextTxnId_{1};

    void recover();
};

}  // namespace storage_engine
