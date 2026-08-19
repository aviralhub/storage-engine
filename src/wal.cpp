#include "storage_engine/wal.hpp"
#include "storage_engine/crc32.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace storage_engine {

WriteAheadLog::WriteAheadLog(const std::string& path) : path_(path) {
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("WriteAheadLog: failed to open " + path_);
    }
    ::lseek(fd_, 0, SEEK_END);
}

WriteAheadLog::~WriteAheadLog() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void WriteAheadLog::crashDuringNextAppendAfter(std::size_t bytes) {
    crash_after_bytes_ = static_cast<long>(bytes);
}

void WriteAheadLog::crashAfterNextAppend() {
    crash_after_full_append_ = true;
}

// [len][lsn][type][key][vlen][value][crc]
int64_t WriteAheadLog::append(WalRecordType type, int64_t key, const std::string& value) {
    int64_t lsn = next_lsn_++;

    std::string body;
    auto put32 = [&](int32_t v) { body.append(reinterpret_cast<const char*>(&v), sizeof(v)); };
    auto put64 = [&](int64_t v) { body.append(reinterpret_cast<const char*>(&v), sizeof(v)); };

    put64(lsn);
    put32(static_cast<int32_t>(type));
    put64(key);
    put32(static_cast<int32_t>(value.size()));
    body.append(value);

    uint32_t checksum = crc32(body.data(), body.size());

    std::string record;
    int32_t bodyLen = static_cast<int32_t>(body.size());
    record.append(reinterpret_cast<const char*>(&bodyLen), sizeof(bodyLen));
    record.append(body);
    record.append(reinterpret_cast<const char*>(&checksum), sizeof(checksum));

    if (crash_after_bytes_ >= 0) {
        std::size_t n = std::min(static_cast<std::size_t>(crash_after_bytes_), record.size());
        ssize_t ignored = ::write(fd_, record.data(), n);
        (void)ignored;
        _exit(1);
    }

    ssize_t written = ::write(fd_, record.data(), record.size());
    if (written < 0 || static_cast<std::size_t>(written) != record.size()) {
        throw std::runtime_error("WriteAheadLog: write failed");
    }
    if (::fsync(fd_) != 0) {
        throw std::runtime_error("WriteAheadLog: fsync failed");
    }

    if (crash_after_full_append_) {
        _exit(1);
    }

    return lsn;
}

void WriteAheadLog::replay(const std::function<void(const WalRecord&)>& apply) {
    int fd = ::open(path_.c_str(), O_RDONLY);
    if (fd < 0) {
        return;
    }

    off_t offset = 0;
    while (true) {
        int32_t bodyLen = 0;
        ssize_t n = ::pread(fd, &bodyLen, sizeof(bodyLen), offset);
        if (n != static_cast<ssize_t>(sizeof(bodyLen)) || bodyLen < 0) {
            break;
        }

        std::vector<char> body(static_cast<std::size_t>(bodyLen));
        n = ::pread(fd, body.data(), body.size(), offset + static_cast<off_t>(sizeof(bodyLen)));
        if (n != static_cast<ssize_t>(body.size())) {
            break;
        }

        uint32_t storedChecksum = 0;
        off_t checksumOffset = offset + static_cast<off_t>(sizeof(bodyLen)) + static_cast<off_t>(body.size());
        n = ::pread(fd, &storedChecksum, sizeof(storedChecksum), checksumOffset);
        if (n != static_cast<ssize_t>(sizeof(storedChecksum))) {
            break;
        }

        if (crc32(body.data(), body.size()) != storedChecksum) {
            break;
        }

        std::size_t pos = 0;
        auto get64 = [&] {
            int64_t v;
            std::memcpy(&v, body.data() + pos, sizeof(v));
            pos += sizeof(v);
            return v;
        };
        auto get32 = [&] {
            int32_t v;
            std::memcpy(&v, body.data() + pos, sizeof(v));
            pos += sizeof(v);
            return v;
        };

        WalRecord rec;
        rec.lsn = get64();
        rec.type = static_cast<WalRecordType>(get32());
        rec.key = get64();
        int32_t vlen = get32();
        rec.value.assign(body.data() + pos, static_cast<std::size_t>(vlen));

        apply(rec);
        if (rec.lsn >= next_lsn_) {
            next_lsn_ = rec.lsn + 1;
        }

        offset = checksumOffset + static_cast<off_t>(sizeof(storedChecksum));
    }
    ::close(fd);
}

void WriteAheadLog::reset() {
    ::close(fd_);
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("WriteAheadLog: failed to reopen " + path_ + " for truncation");
    }
    next_lsn_ = 1;
}

}  // namespace storage_engine
