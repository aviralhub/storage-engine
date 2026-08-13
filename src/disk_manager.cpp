#include "storage_engine/disk_manager.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>

namespace storage_engine {

DiskManager::DiskManager(const std::string& db_file_path) {
    fd_ = ::open(db_file_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("DiskManager: failed to open " + db_file_path);
    }

    off_t size = ::lseek(fd_, 0, SEEK_END);
    if (size < 0) {
        throw std::runtime_error("DiskManager: lseek failed on " + db_file_path);
    }
    next_page_id_ = static_cast<int32_t>(size / static_cast<off_t>(kPageSize));
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void DiskManager::readPage(int32_t page_id, char* out) const {
    off_t offset = static_cast<off_t>(page_id) * static_cast<off_t>(kPageSize);
    ssize_t n = ::pread(fd_, out, kPageSize, offset);
    if (n < 0) {
        throw std::runtime_error("DiskManager: pread failed on page " + std::to_string(page_id));
    }
    // unwritten pages read back as zeros
    for (std::size_t i = static_cast<std::size_t>(n); i < kPageSize; ++i) {
        out[i] = 0;
    }
}

void DiskManager::writePage(int32_t page_id, const char* data) {
    off_t offset = static_cast<off_t>(page_id) * static_cast<off_t>(kPageSize);
    ssize_t n = ::pwrite(fd_, data, kPageSize, offset);
    if (n < 0 || static_cast<std::size_t>(n) != kPageSize) {
        throw std::runtime_error("DiskManager: pwrite failed on page " + std::to_string(page_id));
    }
}

int32_t DiskManager::allocatePage() {
    return next_page_id_++;
}

void DiskManager::sync() {
    if (::fsync(fd_) != 0) {
        throw std::runtime_error("DiskManager: fsync failed");
    }
}

}  // namespace storage_engine
