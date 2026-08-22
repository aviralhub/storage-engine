#include "storage_engine/engine.hpp"

#include <iostream>
#include <string>

using namespace storage_engine;

namespace {

void fillUpTo(Engine& engine, int64_t n) {
    for (int64_t k = 1; k < n; ++k) {
        engine.put(k, "v" + std::to_string(k));
    }
}

int runCrashTorn(const std::string& prefix, int64_t n) {
    Engine engine(prefix + ".db", prefix + ".wal");
    fillUpTo(engine, n);
    engine.wal().crashDuringNextAppendAfter(10);
    engine.put(n, "v" + std::to_string(n));
    std::cerr << "unreachable: crash injection did not fire\n";
    return 1;
}

int runCrashRedo(const std::string& prefix, int64_t n) {
    Engine engine(prefix + ".db", prefix + ".wal");
    fillUpTo(engine, n);
    engine.wal().crashAfterNextAppend();
    engine.put(n, "v" + std::to_string(n));
    std::cerr << "unreachable: crash injection did not fire\n";
    return 1;
}

int verifyTorn(const std::string& prefix, int64_t n) {
    Engine engine(prefix + ".db", prefix + ".wal");
    for (int64_t k = 1; k < n; ++k) {
        if (engine.get(k) != "v" + std::to_string(k)) {
            std::cerr << "missing or wrong key " << k << "\n";
            return 1;
        }
    }
    if (engine.get(n).has_value()) {
        std::cerr << "key " << n << " should have been discarded (torn write) but is present\n";
        return 1;
    }
    std::cout << "torn-write recovery verified: keys 1.." << (n - 1) << " intact, key " << n
              << " correctly discarded\n";
    return 0;
}

int verifyRedo(const std::string& prefix, int64_t n) {
    Engine engine(prefix + ".db", prefix + ".wal");
    for (int64_t k = 1; k <= n; ++k) {
        if (engine.get(k) != "v" + std::to_string(k)) {
            std::cerr << "missing or wrong key " << k << "\n";
            return 1;
        }
    }
    std::cout << "redo recovery verified: all keys 1.." << n << " present after crash-before-apply\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: crash_harness <crash-torn|crash-redo|verify-torn|verify-redo> <db_prefix> <n>\n";
        return 2;
    }
    std::string mode = argv[1];
    std::string prefix = argv[2];
    int64_t n = std::stoll(argv[3]);

    if (mode == "crash-torn") return runCrashTorn(prefix, n);
    if (mode == "crash-redo") return runCrashRedo(prefix, n);
    if (mode == "verify-torn") return verifyTorn(prefix, n);
    if (mode == "verify-redo") return verifyRedo(prefix, n);

    std::cerr << "unknown mode: " << mode << "\n";
    return 2;
}
