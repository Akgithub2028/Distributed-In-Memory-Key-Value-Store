# Replication and High Availability

## Primary-Replica Synchronization

We will implement a simple but robust **primary-replica replication pipeline**.
The C++ server can be started in either `primary` or `replica` mode.

### Phase A: Fire-and-Forget Replication
1. The replica connects to the primary via TCP.
2. The replica sends a sync request command (e.g., `SYNC`).
3. The primary accepts the connection, and from that point forward, forwards all successfully executed write commands to the replica socket stream.
4. The replica receives the RESP commands and executes them against its own storage engine.

### Phase B: Offset Tracking
1. The primary maintains a logical replication offset, incremented for each byte written to the replication stream.
2. The replica tracks its own offset.
3. If the connection drops, the replica reconnects with its last known offset.
4. If the primary still has the required log slice in memory (a bounded replication backlog buffer), it replays the missing writes. If not, a full sync is triggered (which ties into Phase 2 Persistence snapshots).

### Limitations
- No automatic failover or leader election is planned in the C++ layer.
- A Python sentinel-style script could be used for external failover orchestration if time permits.
