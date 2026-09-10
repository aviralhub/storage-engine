# Design notes

## Pages

Fixed 4KB pages. A leaf holds up to 32 keys and each value gets a fixed 112-byte slot (111 bytes plus
the NUL), so longer values are truncated. A slotted page would waste less space but split and merge
would have to work on bytes instead of key counts, which didn't seem worth it.

Key arrays have one extra slot because insert overflows a node by one before splitting it. ASan
caught that.

Pages freed by a merge aren't reused, so the file only grows.

## pread/pwrite, not mmap

With mmap the kernel decides when dirty pages get written, so a page could hit disk before its WAL
record. `writePage` doesn't fsync. Durability comes from fsyncing the WAL, and pages only get fsynced
at checkpoint.

## WAL

`put` and `remove` append a CRC32'd record and fsync it before touching the tree. Recovery replays
from the start and stops at the first record that's short or fails its checksum. Everything before
that point is intact, anything after is a torn write.

Redo only. Every record is a complete put or remove, so there's nothing to undo, and replaying one
twice is harmless: puts overwrite and removing a missing key does nothing.

## Crash test

`tools/crash_harness.cpp` makes `WriteAheadLog::append` call `_exit()`:

- partway through a record, before fsync: recovery drops the partial record
- after fsync, before the tree insert: recovery replays it

Exiting at a fixed point hits the fsync boundary every run, which `kill -9` at a random moment
wouldn't. This only covers the process dying, not power loss.

## Benchmarks

The first run used `/tmp` and reported ~1.36M fsynced inserts/sec with a 0us p50. `/tmp` is tmpfs
in WSL2 so fsync did nothing. The benchmark now uses ext4 under `/root`. `/mnt/d` goes through the
Windows filesystem, so that's out too.

## Locking

Per-key S/X locks held until commit or abort (strict 2PL). Keys rather than pages because `Engine`
only deals in keys and page ids belong to the tree.

The tree and buffer pool aren't thread-safe, so `Engine` puts all tree access behind one mutex. The
locks give isolation between transactions but the index itself isn't concurrent; that would need
latch crabbing.

Transactions buffer their writes and apply them with one fsync at commit, so the WAL still never
sees anything uncommitted and redo-only still works. A transaction doesn't see its own writes
before commit.

`grant()` used to overwrite a held lock's mode, so a txn holding X that asked for S on the same key
got downgraded. It only upgrades now.

## Deadlock detection

Before blocking, `lock()` walks from the holders of the resource through whatever each of them is
waiting on. If that reaches the requester, the requester aborts instead of waiting. The only state
kept is which resource each blocked txn is waiting on; edges are worked out from the current holders
every time, so nothing goes stale when a txn finishes.

Always aborting the requester is simple and easy to test, but it isn't fair: the same txn can lose
over and over. With 8 threads on a single key over 99% of attempts abort. Backoff or aborting the
youngest txn would help.

This went in after a test doing `txnGet` then `txnPut` on one key from several threads hung. Every
thread held S and was waiting to upgrade.
