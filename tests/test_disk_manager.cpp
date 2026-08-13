#include "storage_engine/disk_manager.hpp"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

namespace {

std::string freshDbPath(const std::string& name) {
    std::string path = "test_" + name + ".db";
    std::remove(path.c_str());
    return path;
}

}  // namespace

TEST_CASE("a freshly created file has no pages allocated yet", "[disk_manager]") {
    DiskManager dm(freshDbPath("fresh"));
    REQUIRE(dm.allocatePage() == 0);
    REQUIRE(dm.allocatePage() == 1);
}

TEST_CASE("a written page reads back with the same bytes", "[disk_manager]") {
    DiskManager dm(freshDbPath("readwrite"));
    int32_t page = dm.allocatePage();

    char written[kPageSize];
    std::memset(written, 0, kPageSize);
    std::strcpy(written, "hello page");

    dm.writePage(page, written);

    char readBack[kPageSize];
    dm.readPage(page, readBack);
    REQUIRE(std::memcmp(written, readBack, kPageSize) == 0);
}

TEST_CASE("writing to one page does not disturb its neighbours", "[disk_manager]") {
    DiskManager dm(freshDbPath("neighbours"));
    int32_t a = dm.allocatePage();
    int32_t b = dm.allocatePage();
    int32_t c = dm.allocatePage();

    char pageA[kPageSize];
    char pageC[kPageSize];
    std::memset(pageA, 'A', kPageSize);
    std::memset(pageC, 'C', kPageSize);
    dm.writePage(a, pageA);
    dm.writePage(c, pageC);

    char readB[kPageSize];
    dm.readPage(b, readB);
    char zeros[kPageSize];
    std::memset(zeros, 0, kPageSize);
    REQUIRE(std::memcmp(readB, zeros, kPageSize) == 0);

    char readA[kPageSize];
    dm.readPage(a, readA);
    REQUIRE(std::memcmp(readA, pageA, kPageSize) == 0);
}

TEST_CASE("reopening the same file continues page allocation where it left off", "[disk_manager]") {
    std::string path = freshDbPath("reopen");
    {
        DiskManager dm(path);
        dm.allocatePage();
        dm.allocatePage();
        char page[kPageSize];
        std::memset(page, 0, kPageSize);
        dm.writePage(1, page);
    }
    {
        DiskManager dm(path);
        REQUIRE(dm.allocatePage() == 2);
    }
}
