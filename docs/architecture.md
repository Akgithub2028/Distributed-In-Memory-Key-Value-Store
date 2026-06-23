# Architecture: C++20 Distributed In-Memory KV Store

This document outlines the architectural decisions and underlying mechanics of the Redis-inspired custom KV store. The system is designed to be highly concurrent, memory-efficient, deeply persistent, and distributed.

## 1. Network Layer & Connection Handling
The server utilizes a **TCP multiplexing** approach built entirely from standard POSIX sockets. 
- **Thread-Per-Connection Strategy**: Currently, the server accepts client TCP connections and spawns a lightweight `std::thread` to handle the `read/write` loop. 
- **RESP Protocol Serialization**: The system communicates using a fully-compliant custom **RESP (REdis Serialization Protocol)** parser. It supports raw byte-stream chunks and handles complex deeply-nested arrays, bulk strings, integers, simple strings, and errors natively.

## 2. Storage Engine: Sharded Lock-Free Dictionaries
Instead of placing a massive global `std::mutex` around a single `std::unordered_map` (which artificially bottlenecks concurrent read/writes), the `StorageEngine` utilizes **Dictionary Sharding**.

- **Sharding Mechanism**: The core index is split into an array of 16 distinct `Shard` objects. When a key is inserted, the engine computes its hash modulo 16, immediately locking only that specific shard using a `std::shared_mutex`.
- **Concurrent Throughput**: This design allows 16 parallel threads to perform `SET`, `GET`, and `DEL` operations simultaneously with zero lock contention, assuming uniform key distribution.

## 3. Memory Eviction & Approximate LRU
The database tracks total memory usage accurately using `std::atomic<size_t>` counters and a custom `entry_cost()` heuristic. 
If the `maxmemory` limit is reached during a `SET` operation:
- **Random Sampling LRU**: The engine selects 5 random buckets from the target shard and probes their keys. It evaluates the `last_access_ms` property and forcefully evicts the oldest key. This provides exceptional O(1) probabilistic LRU pruning without the massive overhead of a doubly-linked list.

## 4. TTL & Active/Lazy Expiration
Expiration semantics (`EXPIRE`, `PEXPIREAT`) are handled with two parallel strategies:
1. **Lazy Expiration**: If a client calls `GET` on a key whose absolute TTL boundary has passed, the engine purges the key immediately and returns `std::nullopt` (simulating it was never there).
2. **Background Sweeper**: A low-priority background thread wakes up repeatedly, locking shards probabilistically and purging dead keys to prevent un-accessed dead data from bloating memory limits.

## 5. Durability: Append-Only File (AOF) & Background fsync
Disk durability is guaranteed via standard AOF techniques.
- Every mutating command (`SET`, `DEL`, `INCR`, `PERSIST`) is serialized back into raw RESP and appended to `appendonly.aof`.
- **Optimization (`everysec`)**: To prevent synchronous disk I/O from bottlenecking the network throughput, the `AOFLogger` relies on OS-level buffering and utilizes a dedicated background thread to trigger `std::ofstream::flush()` exactly once per second.
- **Startup Replay**: On crash and restart, the server reads the AOF byte-by-byte into the `RESPParser` and replays the entire history deterministically before opening the socket listener.

## 6. Distributed Streaming: AOF Replication Backlog
A fully-functioning **Primary-Replica Pipeline** is built natively on top of the AOF structure:
- **The Handshake**: A Replica initiates connection with `PSYNC <offset>`. The offset is simply the byte-size of the Replica's own local `appendonly.aof` file.
- **The Stream**: The Primary skips maintaining an expensive memory ring-buffer. Instead, it opens its own AOF file, `seek()`s to the requested offset, and enters a `tail -f` stream, blasting file delta bytes directly across the TCP socket to the Replica.
- **The Mirror**: The Replica dumps these bytes into its own AOF (ensuring a bit-for-bit perfect backup) and executes the commands to achieve memory convergence.
