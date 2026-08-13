#include "storage_engine/buffer_pool.hpp"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace storage_engine;

namespace {

std::string freshDbPath(const std::string& name) {
    std::string path = "test_bp_" + name + ".db";
    std::remove(path.c_str());
    return path;
}

}  // namespace

TEST_CASE("a new page can be written to and read back after unpinning", "[buffer_pool]") {
    DiskManager dm(freshDbPath("basic"));
    BufferPool pool(dm, 4);

    auto [id, data] = pool.newPage();
    std::strcpy(data, "hello");
    pool.unpinPage(id, true);

    char* fetched = pool.fetchPage(id);
    REQUIRE(std::string(fetched) == "hello");
    pool.unpinPage(id, false);
}

TEST_CASE("fetching the same page twice returns the same buffer", "[buffer_pool]") {
    DiskManager dm(freshDbPath("samebuf"));
    BufferPool pool(dm, 4);

    auto [id, data] = pool.newPage();
    std::strcpy(data, "same");
    pool.unpinPage(id, true);

    char* a = pool.fetchPage(id);
    char* b = pool.fetchPage(id);
    REQUIRE(a == b);
    pool.unpinPage(id, false);
    pool.unpinPage(id, false);
}

TEST_CASE("a full pool of pinned pages refuses to fetch one more", "[buffer_pool]") {
    DiskManager dm(freshDbPath("exhausted"));
    BufferPool pool(dm, 2);

    pool.newPage();
    pool.newPage();
    REQUIRE_THROWS(pool.newPage());
}

TEST_CASE("evicting the least recently used page writes it back if dirty", "[buffer_pool]") {
    DiskManager dm(freshDbPath("lru"));
    BufferPool pool(dm, 2);

    auto [idA, dataA] = pool.newPage();
    std::strcpy(dataA, "A");
    pool.unpinPage(idA, true);

    auto [idB, dataB] = pool.newPage();
    std::strcpy(dataB, "B");
    pool.unpinPage(idB, true);

    auto [idC, dataC] = pool.newPage();
    std::strcpy(dataC, "C");
    pool.unpinPage(idC, true);

    char raw[kPageSize];
    dm.readPage(idA, raw);
    REQUIRE(std::string(raw) == "A");
}

TEST_CASE("re-fetching a page moves it back out of eviction order", "[buffer_pool]") {
    DiskManager dm(freshDbPath("reorder"));
    BufferPool pool(dm, 2);

    auto [idA, dataA] = pool.newPage();
    std::strcpy(dataA, "A");
    pool.unpinPage(idA, true);

    auto [idB, dataB] = pool.newPage();
    std::strcpy(dataB, "B");
    pool.unpinPage(idB, true);

    char* touched = pool.fetchPage(idA);
    REQUIRE(std::string(touched) == "A");
    pool.unpinPage(idA, false);

    auto [idC, dataC] = pool.newPage();
    std::strcpy(dataC, "C");
    pool.unpinPage(idC, true);

    char raw[kPageSize];
    dm.readPage(idB, raw);
    REQUIRE(std::string(raw) == "B");
}

TEST_CASE("a pinned page survives eviction pressure that clears everything else", "[buffer_pool]") {
    DiskManager dm(freshDbPath("pinned"));
    BufferPool pool(dm, 3);

    auto [idA, dataA] = pool.newPage();
    std::strcpy(dataA, "A");

    auto [idB, dataB] = pool.newPage();
    pool.unpinPage(idB, true);
    auto [idC, dataC] = pool.newPage();
    pool.unpinPage(idC, true);

    auto [idD, dataD] = pool.newPage();
    pool.unpinPage(idD, true);

    char* stillA = pool.fetchPage(idA);
    REQUIRE(std::string(stillA) == "A");
    pool.unpinPage(idA, false);

    REQUIRE_NOTHROW(pool.newPage());
}

TEST_CASE("a full pool where every page is still pinned refuses to fetch one more", "[buffer_pool]") {
    DiskManager dm(freshDbPath("allpinned"));
    BufferPool pool(dm, 2);

    pool.newPage();
    pool.newPage();
    REQUIRE_THROWS(pool.newPage());
}
