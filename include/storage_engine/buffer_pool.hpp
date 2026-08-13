#pragma once

#include "storage_engine/disk_manager.hpp"

#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

namespace storage_engine {

class BufferPool {
public:
    BufferPool(DiskManager& disk_manager, std::size_t pool_size);

    char* fetchPage(int32_t page_id);
    std::pair<int32_t, char*> newPage();
    void unpinPage(int32_t page_id, bool is_dirty);
    void flushPage(int32_t page_id);
    void flushAll();
    int32_t pageCount() const { return disk_manager_.pageCount(); }

private:
    struct Frame {
        alignas(8) char data[kPageSize];
        int32_t page_id = kInvalidPageId;
        int pin_count = 0;
        bool dirty = false;
    };

    DiskManager& disk_manager_;
    std::vector<Frame> frames_;
    std::unordered_map<int32_t, std::size_t> page_table_;
    std::list<std::size_t> lru_list_;
    std::unordered_map<std::size_t, std::list<std::size_t>::iterator> lru_map_;
    std::vector<std::size_t> free_frames_;

    std::size_t grabFrame();
};

}  // namespace storage_engine
