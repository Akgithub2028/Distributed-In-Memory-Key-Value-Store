import socket
import unittest
import time
import subprocess
import os

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

def parse_info(info_str):
    res = {}
    lines = info_str.split('\r\n')
    for line in lines:
        if line and not line.startswith('#'):
            parts = line.split(':')
            if len(parts) == 2:
                res[parts[0]] = parts[1]
    return res

class TestObservability(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.path.exists("appendonly.aof"):
            os.remove("appendonly.aof")
        
        cls.server_proc = subprocess.Popen(
            ["./build/redis_kv_store", "--port", "6381"],
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
        self.sock.connect(('127.0.0.1', 6381))

    def tearDown(self):
        self.sock.close()

    def test_clients_tracking(self):
        # We already have self.sock connected, so at least 1 client
        info = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$7\r\nCLIENTS\r\n")
        stats = parse_info(info)
        clients = int(stats.get('connected_clients', 0))
        self.assertTrue(clients >= 1)
        
        # Connect another client
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.connect(('127.0.0.1', 6381))
        time.sleep(0.1) # Wait for accept
        
        info = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$7\r\nCLIENTS\r\n")
        stats2 = parse_info(info)
        self.assertEqual(int(stats2['connected_clients']), clients + 1)
        
        sock2.close()
        time.sleep(0.1) # Wait for thread to exit
        
        info = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$7\r\nCLIENTS\r\n")
        stats3 = parse_info(info)
        self.assertEqual(int(stats3['connected_clients']), clients)

    def test_hits_and_misses(self):
        # SET a key
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\nstat\r\n$3\r\nval\r\n")
        
        # 3 GET hits
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$4\r\nstat\r\n")
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$4\r\nstat\r\n")
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$4\r\nstat\r\n")
        
        # 2 GET misses
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$7\r\nmissing\r\n")
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$7\r\nmissing\r\n")
        
        # Check stats
        info = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$5\r\nSTATS\r\n")
        stats = parse_info(info)
        
        self.assertTrue(int(stats['keyspace_hits']) >= 3)
        self.assertTrue(int(stats['keyspace_misses']) >= 2)
        self.assertTrue(int(stats['total_commands_processed']) > 0)

    def test_eviction_and_expiration(self):
        # Test expiration
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$6\r\nexpkey\r\n$1\r\nv\r\n")
        send_recv(self.sock, "*3\r\n$6\r\nEXPIRE\r\n$6\r\nexpkey\r\n$1\r\n1\r\n")
        time.sleep(1.2)
        # This GET will trigger lazy expiration
        send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$6\r\nexpkey\r\n")
        
        info = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$5\r\nSTATS\r\n")
        stats = parse_info(info)
        self.assertTrue(int(stats['expired_keys']) >= 1)
        
        # Test Eviction by setting maxmemory super low
        send_recv(self.sock, "*4\r\n$6\r\nCONFIG\r\n$3\r\nSET\r\n$9\r\nMAXMEMORY\r\n$4\r\n1000\r\n")
        
        # Spam inserts to cause evictions (we need enough keys to hit the same shard)
        for i in range(100):
            k = f"evict_key_{i}"
            send_recv(self.sock, f"*3\r\n$3\r\nSET\r\n${len(k)}\r\n{k}\r\n$1\r\nv\r\n")
            
        # Clear limit
        send_recv(self.sock, "*4\r\n$6\r\nCONFIG\r\n$3\r\nSET\r\n$9\r\nMAXMEMORY\r\n$1\r\n0\r\n")
        
        info = send_recv(self.sock, "*2\r\n$4\r\nINFO\r\n$5\r\nSTATS\r\n")
        stats = parse_info(info)
        self.assertTrue(int(stats['evicted_keys']) >= 1)

if __name__ == '__main__':
    unittest.main()
