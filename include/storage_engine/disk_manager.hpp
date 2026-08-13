#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace storage_engine {

constexpr std::size_t kPageSize = 4096;
constexpr int32_t kInvalidPageId = -1;

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file_path);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    void readPage(int32_t page_id, char* out) const;
    void writePage(int32_t page_id, const char* data);
    int32_t allocatePage();
    int32_t pageCount() const { return next_page_id_; }

    void sync();

private:
    int fd_;
    int32_t next_page_id_;
};

}  // namespace storage_engine
