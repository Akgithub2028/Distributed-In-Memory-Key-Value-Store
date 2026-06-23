import time
import json
import statistics
import subprocess
import os
import threading
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.client import KVClient

def measure_throughput_and_latency(concurrency=10, requests_per_client=1000):
    latencies = []
    
    def worker(client_id):
        client = KVClient(port=6379)
        client.connect()
        for i in range(requests_per_client):
            key = f"bench_k_{client_id}_{i}"
            val = "v" * 64
            
            t0 = time.perf_counter()
            client.set(key, val)
            client.get(key)
            t1 = time.perf_counter()
            
            # Each loop is 2 commands, divide by 2 for average single-command latency
            latencies.append((t1 - t0) * 1000 / 2.0)
            
        client.close()

    t_start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [executor.submit(worker, i) for i in range(concurrency)]
        for f in futures:
            f.result()
    t_end = time.perf_counter()
    
    total_time = t_end - t_start
    total_commands = concurrency * requests_per_client * 2
    throughput = total_commands / total_time
    
    latencies.sort()
    return {
        "throughput_req_sec": throughput,
        "p50_latency_ms": latencies[int(len(latencies) * 0.50)],
        "p95_latency_ms": latencies[int(len(latencies) * 0.95)],
        "p99_latency_ms": latencies[int(len(latencies) * 0.99)]
    }

def measure_recovery_time():
    # Make sure server is killed and restart it.
    os.system("killall redis_kv_store 2>/dev/null")
    time.sleep(0.5)
    
    # Measure time to start up and reply to PING
    t0 = time.perf_counter()
    
    proc = subprocess.Popen(
        ["./build/redis_kv_store", "--port", "6379"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    
    client = KVClient(port=6379)
    # Poll until ready
    while True:
        try:
            client.connect()
            resp = client.ping()
            if resp == "PONG":
                break
        except Exception:
            time.sleep(0.01)
            
    t1 = time.perf_counter()
    client.close()
    
    # Clean up
    proc.terminate()
    proc.wait()
    
    return (t1 - t0) * 1000 # ms

def measure_replication_lag():
    # Clean AOFs
    os.system("rm -f appendonly.aof replica_aof.aof")
    
    # Start Primary
    primary = subprocess.Popen(
        ["./build/redis_kv_store", "--port", "6379"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    time.sleep(0.5)
    
    # Start Replica (we run in same dir, wait, they will overwrite AOF!)
    # Actually, if we just start it without AOF persist, but we can't.
    # Let's run replica in a temp dir
    os.makedirs("bench_replica_dir", exist_ok=True)
    replica = subprocess.Popen(
        ["../build/redis_kv_store", "--port", "6380", "--replicaof", "127.0.0.1", "6379"],
        cwd="bench_replica_dir",
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    time.sleep(0.5)
    
    p_client = KVClient(port=6379)
    p_client.connect()
    r_client = KVClient(port=6380)
    r_client.connect()
    
    lags = []
    for i in range(100):
        k = f"rep_{i}"
        v = "v"
        
        # We start timer, write to primary, and immediately spin-poll replica
        t0 = time.perf_counter()
        p_client.set(k, v)
        
        while True:
            val = r_client.get(k)
            if val == v:
                t1 = time.perf_counter()
                lags.append((t1 - t0) * 1000)
                break
    
    p_client.close()
    r_client.close()
    primary.terminate()
    replica.terminate()
    
    lags.sort()
    return {
        "avg_lag_ms": sum(lags) / len(lags),
        "p99_lag_ms": lags[int(len(lags) * 0.99)]
    }

def main():
    print("Running Redis-like KV Store Benchmark Suite...")
    
    # 1. Throughput and Latency
    print("\nStarting primary server for throughput tests...")
    # Clean AOF to start fresh
    os.system("rm -f appendonly.aof")
    primary = subprocess.Popen(
        ["./build/redis_kv_store", "--port", "6379"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    time.sleep(0.5)
    
    print("Measuring Throughput and Latency (10 threads, 1000 reqs/thread)...")
    tl_results = measure_throughput_and_latency(10, 1000)
    print(f"  Throughput: {tl_results['throughput_req_sec']:.2f} req/s")
    print(f"  P50 Latency: {tl_results['p50_latency_ms']:.3f} ms")
    print(f"  P95 Latency: {tl_results['p95_latency_ms']:.3f} ms")
    print(f"  P99 Latency: {tl_results['p99_latency_ms']:.3f} ms")
    
    primary.terminate()
    primary.wait()
    
    # 2. Recovery Time
    # (The AOF now has 20,000 commands in it from the throughput test!)
    print(f"\nMeasuring AOF Recovery Time (replaying 20,000 operations)...")
    recovery_ms = measure_recovery_time()
    print(f"  Recovery Time: {recovery_ms:.2f} ms")
    
    # 3. Replication Lag
    print("\nMeasuring Replication Lag...")
    lag_results = measure_replication_lag()
    print(f"  Avg Lag: {lag_results['avg_lag_ms']:.3f} ms")
    print(f"  P99 Lag: {lag_results['p99_lag_ms']:.3f} ms")
    
    # Dump results
    results = {
        "throughput_and_latency": tl_results,
        "recovery_time_ms": recovery_ms,
        "replication_lag": lag_results
    }
    
    with open("benchmark_results.json", "w") as f:
        json.dump(results, f, indent=4)
        
    print("\nBenchmarks complete! Results saved to benchmark_results.json.")
    
if __name__ == "__main__":
    main()
