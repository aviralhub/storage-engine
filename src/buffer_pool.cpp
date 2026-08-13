#include "storage_engine/buffer_pool.hpp"

#include <cstring>
#include <stdexcept>

namespace storage_engine {

BufferPool::BufferPool(DiskManager& disk_manager, std::size_t pool_size)
    : disk_manager_(disk_manager), frames_(pool_size) {
    for (std::size_t i = 0; i < pool_size; ++i) {
        free_frames_.push_back(i);
    }
}

std::size_t BufferPool::grabFrame() {
    if (!free_frames_.empty()) {
        std::size_t idx = free_frames_.back();
        free_frames_.pop_back();
        return idx;
    }
    if (lru_list_.empty()) {
        throw std::runtime_error("BufferPool: no free frame, every page is pinned");
    }
    std::size_t idx = lru_list_.front();
    lru_list_.pop_front();
    lru_map_.erase(idx);

    Frame& victim = frames_[idx];
    if (victim.dirty) {
        disk_manager_.writePage(victim.page_id, victim.data);
    }
    page_table_.erase(victim.page_id);
    return idx;
}

char* BufferPool::fetchPage(int32_t page_id) {
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        std::size_t idx = it->second;
        auto lruIt = lru_map_.find(idx);
        if (lruIt != lru_map_.end()) {
            lru_list_.erase(lruIt->second);
            lru_map_.erase(lruIt);
        }
        frames_[idx].pin_count++;
        return frames_[idx].data;
    }

    std::size_t idx = grabFrame();
    Frame& frame = frames_[idx];
    disk_manager_.readPage(page_id, frame.data);
    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.dirty = false;
    page_table_[page_id] = idx;
    return frame.data;
}

std::pair<int32_t, char*> BufferPool::newPage() {
    int32_t page_id = disk_manager_.allocatePage();
    std::size_t idx = grabFrame();
    Frame& frame = frames_[idx];
    std::memset(frame.data, 0, kPageSize);
    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.dirty = true;
    page_table_[page_id] = idx;
    return {page_id, frame.data};
}

void BufferPool::unpinPage(int32_t page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;
    }
    Frame& frame = frames_[it->second];
    if (is_dirty) {
        frame.dirty = true;
    }
    if (frame.pin_count > 0) {
        frame.pin_count--;
    }
    if (frame.pin_count == 0) {
        lru_list_.push_back(it->second);
        lru_map_[it->second] = std::prev(lru_list_.end());
    }
}

void BufferPool::flushPage(int32_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return;
    }
    Frame& frame = frames_[it->second];
    if (frame.dirty) {
        disk_manager_.writePage(frame.page_id, frame.data);
        frame.dirty = false;
    }
}

void BufferPool::flushAll() {
    for (auto& [page_id, idx] : page_table_) {
        Frame& frame = frames_[idx];
        if (frame.dirty) {
            disk_manager_.writePage(frame.page_id, frame.data);
            frame.dirty = false;
        }
    }
}

}  // namespace storage_engine
