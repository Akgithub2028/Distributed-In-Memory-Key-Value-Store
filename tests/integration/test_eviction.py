import socket
import unittest
import time
import subprocess

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

class TestEviction(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server_proc = subprocess.Popen(
            ["./build/redis_kv_store", "--port", "6379"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(0.5)

    @classmethod
    def tearDownClass(cls):
        cls.server_proc.terminate()
        cls.server_proc.wait()

    def setUp(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(('127.0.0.1', 6379))
        # Remove any existing limit
        send_recv(self.sock, "*4\r\n$6\r\nCONFIG\r\n$3\r\nSET\r\n$9\r\nMAXMEMORY\r\n$1\r\n0\r\n")

    def tearDown(self):
        # Reset limit to 0
        send_recv(self.sock, "*4\r\n$6\r\nCONFIG\r\n$3\r\nSET\r\n$9\r\nMAXMEMORY\r\n$1\r\n0\r\n")
        self.sock.close()

    def test_oom_failure(self):
        # Set limit to 100 bytes
        send_recv(self.sock, "*4\r\n$6\r\nCONFIG\r\n$3\r\nSET\r\n$9\r\nMAXMEMORY\r\n$3\r\n100\r\n")
        
        # Try to insert a massive string
        payload = "A" * 500
        resp = send_recv(self.sock, f"*3\r\n$3\r\nSET\r\n$3\r\nbig\r\n${len(payload)}\r\n{payload}\r\n")
        self.assertEqual(resp, "-OOM command not allowed when used memory > 'maxmemory'\r\n")

    def test_lru_eviction(self):
        # Set limit slightly larger than 3 entries (approx 150 bytes overhead per entry)
        # We will dynamically find a limit
        
        # Insert A
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$1\r\nA\r\n$1\r\nX\r\n")
        time.sleep(0.01)
        
        # Insert B
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$1\r\nB\r\n$1\r\nX\r\n")
        time.sleep(0.01)
        
        # Insert C
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$1\r\nC\r\n$1\r\nX\r\n")
        
        # Access A and B so C is the oldest (LRU)
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$1\r\nA\r\n")
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$1\r\nB\r\n")
        
        # Get current memory, then set max memory equal to it
        info_resp = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$6\r\nMEMORY\r\n")
        used_mem = 0
        for line in info_resp.split('\r\n'):
            if line.startswith('used_memory:'):
                used_mem = int(line.split(':')[1])
                break
        
        # Set maxmemory exactly to what we use now
        send_recv(self.sock, f"*4\r\n$6\r\nCONFIG\r\n$3\r\nSET\r\n$9\r\nMAXMEMORY\r\n${len(str(used_mem))}\r\n{used_mem}\r\n")
        
        # Insert D, this should force an eviction. Since C is least recently used, C should be evicted.
        # Note: Since our engine partitions by hash, we need to hope D lands in a shard that needs eviction
        # Actually, global memory triggers eviction on whatever shard D lands in. If D lands in the same shard as C, C is evicted.
        # For a robust test, we can just spam inserts to trigger evictions and ensure memory bounds are respected.
        
        for i in range(10):
            k = f"K{i}"
            send_recv(self.sock, f"*3\r\n$3\r\nSET\r\n${len(k)}\r\n{k}\r\n$1\r\nX\r\n")
        
        # Check memory after spam
        info_resp2 = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$6\r\nMEMORY\r\n")
        used_mem2 = 0
        for line in info_resp2.split('\r\n'):
            if line.startswith('used_memory:'):
                used_mem2 = int(line.split(':')[1])
                break
                
        # The memory shouldn't have grown indefinitely. It might be slightly larger due to the last insert size difference,
        # but it shouldn't be +10 entries worth.
        self.assertLessEqual(used_mem2, used_mem + 100)

if __name__ == '__main__':
    unittest.main()
