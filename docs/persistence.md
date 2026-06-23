# Persistence Design

## Phase 1: Append-Only File (AOF)

The system will implement AOF as the primary durability mechanism.
In this approach, every mutating command (`SET`, `DEL`, `EXPIRE`, etc.) is serialized in RESP format and appended to a file (`server.aof`).

### Fsync Policies
The server will support the following `fsync` policies to balance throughput and safety:
1. **Always:** `fsync` is called after every write. Highest durability, lowest throughput.
2. **Everysec (Default):** A background thread calls `fsync` every second. Good compromise between durability and performance.
3. **No:** Let the OS manage flushing buffers to disk.

### Recovery
On startup, the server simply reads `server.aof` from start to finish, parsing the RESP payload and feeding it back into the command execution pipeline.

## Phase 2: Snapshots (Future)
When the system is stable, we will introduce periodic snapshots (similar to RDB). 
- The entire memory state is serialized to a compact binary or line-based format.
- To recover, the snapshot is loaded first, followed by replaying only the tail of the AOF (commands executed after the snapshot began).
