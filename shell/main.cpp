
#include "storage_engine/engine.hpp"

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace storage_engine;

namespace {

std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

std::optional<int64_t> parseKey(const std::string& s) {
    try {
        std::size_t pos;
        int64_t v = std::stoll(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

void printHelp() {
    std::cout <<
        "commands:\n"
        "  SET <key> <value...>   store a value (value is everything after the key)\n"
        "  GET <key>              print the value for a key, or (nil)\n"
        "  DEL <key>              remove a key\n"
        "  SCAN <low> <high>      print every key/value with low <= key <= high\n"
        "  BEGIN                  start buffering SET/DEL until COMMIT or ABORT\n"
        "  COMMIT                 apply everything buffered since BEGIN, one fsync total\n"
        "  ABORT                  discard everything buffered since BEGIN\n"
        "  HELP                   this message\n"
        "  EXIT / QUIT            leave the shell\n"
        "note: inside a transaction, GET/SCAN still read committed state only -\n"
        "      your own buffered writes aren't visible until COMMIT\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dbPath = argc > 1 ? argv[1] : "storage_engine.db";
    std::string walPath = argc > 2 ? argv[2] : "storage_engine.wal";

    Engine engine(dbPath, walPath);
    std::cout << "storage-engine shell - db=" << dbPath << " wal=" << walPath << "\n";
    std::cout << "type HELP for commands\n";

    bool inTransaction = false;
    std::vector<Engine::BatchOp> pending;

    std::string line;
    while (true) {
        std::cout << (inTransaction ? "(tx)> " : "> ");
        if (!std::getline(std::cin, line)) break;

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        for (char& c : cmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (cmd == "EXIT" || cmd == "QUIT") {
            break;
        } else if (cmd == "HELP") {
            printHelp();
        } else if (cmd == "BEGIN") {
            if (inTransaction) {
                std::cout << "error: already in a transaction\n";
            } else {
                inTransaction = true;
                pending.clear();
                std::cout << "OK\n";
            }
        } else if (cmd == "COMMIT") {
            if (!inTransaction) {
                std::cout << "error: not in a transaction\n";
            } else {
                engine.applyBatch(pending);
                std::cout << "OK (" << pending.size() << " operations committed)\n";
                pending.clear();
                inTransaction = false;
            }
        } else if (cmd == "ABORT") {
            if (!inTransaction) {
                std::cout << "error: not in a transaction\n";
            } else {
                std::cout << "OK (" << pending.size() << " operations discarded)\n";
                pending.clear();
                inTransaction = false;
            }
        } else if (cmd == "SET") {
            if (tokens.size() < 3) {
                std::cout << "usage: SET <key> <value...>\n";
                continue;
            }
            auto key = parseKey(tokens[1]);
            if (!key) {
                std::cout << "error: key must be an integer\n";
                continue;
            }
            std::string value;
            for (std::size_t i = 2; i < tokens.size(); ++i) {
                if (i > 2) value += ' ';
                value += tokens[i];
            }
            if (inTransaction) {
                pending.push_back({WalRecordType::Put, *key, value});
                std::cout << "QUEUED\n";
            } else {
                engine.put(*key, value);
                std::cout << "OK\n";
            }
        } else if (cmd == "GET") {
            if (tokens.size() != 2) {
                std::cout << "usage: GET <key>\n";
                continue;
            }
            auto key = parseKey(tokens[1]);
            if (!key) {
                std::cout << "error: key must be an integer\n";
                continue;
            }
            auto value = engine.get(*key);
            std::cout << (value ? *value : "(nil)") << "\n";
        } else if (cmd == "DEL") {
            if (tokens.size() != 2) {
                std::cout << "usage: DEL <key>\n";
                continue;
            }
            auto key = parseKey(tokens[1]);
            if (!key) {
                std::cout << "error: key must be an integer\n";
                continue;
            }
            if (inTransaction) {
                pending.push_back({WalRecordType::Delete, *key, ""});
                std::cout << "QUEUED\n";
            } else {
                bool existed = engine.remove(*key);
                std::cout << (existed ? "OK\n" : "(nil)\n");
            }
        } else if (cmd == "SCAN") {
            if (tokens.size() != 3) {
                std::cout << "usage: SCAN <low> <high>\n";
                continue;
            }
            auto low = parseKey(tokens[1]);
            auto high = parseKey(tokens[2]);
            if (!low || !high) {
                std::cout << "error: low/high must be integers\n";
                continue;
            }
            auto results = engine.rangeScan(*low, *high);
            for (const auto& [k, v] : results) {
                std::cout << k << " = " << v << "\n";
            }
            std::cout << "(" << results.size() << " results)\n";
        } else {
            std::cout << "unknown command: " << tokens[0] << " (try HELP)\n";
        }
    }

    return 0;
}
