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

## Benchmarks

`./build/bench/bench_engine` on WSL2, ext4:

```
=== Insert throughput/latency (per-commit fsync vs group commit) ===
sequential insert (fsync/op) ops=   3000       382 ops/sec   p50=  2353us  p99=   5928us  max=   19336us
random insert (fsync/op)     ops=   3000       347 ops/sec   p50=  2439us  p99=   7117us  max=   15771us
random insert (batch=100)    batches=  30     31053 effective ops/sec (batch size 100)

=== Point lookup / range scan (no WAL involved - reads only) ===
point lookup (random)        ops=  20000    663293 ops/sec   p50=     1us  p99=      3us  max=     111us
range scan of 10         elements:    0.013 ms total,     755629 elements/sec
range scan of 100        elements:    0.016 ms total,    6416838 elements/sec
range scan of 1000       elements:    0.222 ms total,    4502172 elements/sec
range scan of 10000      elements:    0.785 ms total,   12731507 elements/sec

=== Recovery time vs uncheckpointed WAL size ===
recovery with    100 uncheckpointed WAL records:     3.18 ms
recovery with   1000 uncheckpointed WAL records:     7.96 ms
recovery with  10000 uncheckpointed WAL records:    27.62 ms
```

Batching 100 writes behind one fsync is about 90x faster than an fsync per write, but a crash loses
the whole batch.
