# storage-engine

On-disk B+Tree key-value store in C++. 4KB pages with an LRU buffer pool. Work in progress.

## Build

Linux or WSL2.

```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
ctest
```
