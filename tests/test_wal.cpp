#include "storage_engine/wal.hpp"

#include <cstdio>

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

namespace {

std::string freshWalPath(const std::string& name) {
    std::string path = "test_wal_" + name + ".wal";
    std::remove(path.c_str());
    return path;
}

}  // namespace

TEST_CASE("replaying an empty WAL calls nothing", "[wal]") {
    WriteAheadLog wal(freshWalPath("empty"));
    int calls = 0;
    wal.replay([&](const WalRecord&) { calls++; });
    REQUIRE(calls == 0);
}

TEST_CASE("appended records replay back in order with the right fields", "[wal]") {
    WriteAheadLog wal(freshWalPath("roundtrip"));
    wal.append(WalRecordType::Put, 1, "a");
    wal.append(WalRecordType::Put, 2, "b");
    wal.append(WalRecordType::Delete, 1, "");

    std::vector<WalRecord> seen;
    wal.replay([&](const WalRecord& rec) { seen.push_back(rec); });

    REQUIRE(seen.size() == 3);
    REQUIRE(seen[0].type == WalRecordType::Put);
    REQUIRE(seen[0].key == 1);
    REQUIRE(seen[0].value == "a");
    REQUIRE(seen[1].key == 2);
    REQUIRE(seen[1].value == "b");
    REQUIRE(seen[2].type == WalRecordType::Delete);
    REQUIRE(seen[2].key == 1);
}

TEST_CASE("reset truncates the log so replay sees nothing afterward", "[wal]") {
    std::string path = freshWalPath("reset");
    WriteAheadLog wal(path);
    wal.append(WalRecordType::Put, 1, "a");
    wal.reset();

    int calls = 0;
    wal.replay([&](const WalRecord&) { calls++; });
    REQUIRE(calls == 0);
}

TEST_CASE("a manually corrupted record stops replay there instead of misreading garbage", "[wal]") {
    std::string path = freshWalPath("corrupt");
    {
        WriteAheadLog wal(path);
        wal.append(WalRecordType::Put, 1, "a");
        wal.append(WalRecordType::Put, 2, "b");
    }

    // offset 45 is inside record 2
    FILE* f = std::fopen(path.c_str(), "r+b");
    REQUIRE(f != nullptr);
    std::fseek(f, 45, SEEK_SET);
    int byte = std::fgetc(f);
    std::fseek(f, 45, SEEK_SET);
    std::fputc(byte ^ 0xFF, f);
    std::fclose(f);

    WriteAheadLog wal(path);
    std::vector<WalRecord> seen;
    wal.replay([&](const WalRecord& rec) { seen.push_back(rec); });

    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0].key == 1);
}
