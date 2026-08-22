# storage-engine

Key-value store in C++: a B+Tree over 4KB pages with an LRU buffer pool, and a write-ahead log for
crash recovery.

Keys are `int64_t`, values are strings up to 111 bytes. It's a library, not a server.

## Build

Linux or WSL2.

```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
ctest
```

`ctest` also runs `tests/crash_test.sh`, which kills the process mid-write and checks what recovery
brings back.
