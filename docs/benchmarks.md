# Benchmarking and Test Plan

## Quality Assurance Strategy

This project prioritizes **Correctness before Features**. The Python validation layer acts as our source of truth for protocol compliance and regressions.

### Python Integration Test Suite
We will use Python's `pytest` and the standard `redis-py` client (or raw socket scripts) to validate:
- Basic CRUD operations (`SET`, `GET`, `DEL`)
- Edge cases in parsing malformed RESP requests
- TTL accuracy (creating keys with 10ms expiration and verifying they disappear)
- Concurrent writes to different shards
- Replay validation (killing the server, restarting, and checking data persistence)

### Crash Recovery Tests
A Python harness will start the C++ daemon, write a known workload, send a `SIGKILL`, restart the server, and verify every key was replayed from the AOF correctly.

## Benchmarking Targets

Our goal is not to beat Redis on absolute performance, but to demonstrate a deep understanding of bottlenecks and to deliver stable, measurable results.

We will build a Python benchmark script (`python/benchmark/bench.py`) that uses `asyncio` or multiple threads to hammer the server.

### Metrics of Interest
- **Throughput:** Operations per second (Ops/sec) for a pure `GET` workload vs a pure `SET` workload.
- **Latency Percentiles:** P50, P90, P99 latency on a loopback connection.
- **Memory Overhead:** Total memory usage per 1 million keys.

### Target Objectives (Localhost)
- Survive 1,000+ concurrent connections without crashing.
- Maintain P99 latency under 2ms on local queries.
- Recover from a 100MB AOF log in under a few seconds.
