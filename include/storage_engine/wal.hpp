#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace storage_engine {

enum class WalRecordType : int32_t { Put = 1, Delete = 2 };

struct WalRecord {
    int64_t lsn;
    WalRecordType type;
    int64_t key;
    std::string value;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& path);
    ~WriteAheadLog();

    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;

    int64_t append(WalRecordType type, int64_t key, const std::string& value, bool sync = true);
    void flush();
    void replay(const std::function<void(const WalRecord&)>& apply);
    void reset();

    // crash test only
    void crashDuringNextAppendAfter(std::size_t bytes);
    void crashAfterNextAppend();

private:
    std::string path_;
    int fd_;
    int64_t next_lsn_ = 1;
    long crash_after_bytes_ = -1;
    bool crash_after_full_append_ = false;
};

}  // namespace storage_engine
