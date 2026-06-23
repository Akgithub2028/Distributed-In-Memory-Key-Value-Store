import socket
import unittest
import time
import subprocess
import os

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

class TestReplication(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # We need independent AOF files for primary and replica.
        # However, the code hardcodes "appendonly.aof".
        # If we run them in different directories they won't clash.
        # But for MVP, running in the same dir will overwrite AOF.
        # Wait, the prompt says test replication. Let's make sure the primary and replica run in different dirs, or we just pass the AOF name?
        # A simpler way: just run them in the same dir and they overwrite each other's AOF? No, AOFLogger is hardcoded to "appendonly.aof".
        pass

    def setUp(self):
        # We will create two directories: primary_dir and replica_dir
        os.makedirs("primary_dir", exist_ok=True)
        os.makedirs("replica_dir", exist_ok=True)
        if os.path.exists("primary_dir/appendonly.aof"):
            os.remove("primary_dir/appendonly.aof")
        if os.path.exists("replica_dir/appendonly.aof"):
            os.remove("replica_dir/appendonly.aof")

    def tearDown(self):
        try:
            self.p_proc.terminate()
            self.p_proc.wait()
        except: pass
        try:
            self.r_proc.terminate()
            self.r_proc.wait()
        except: pass

    def test_replication_sync(self):
        # Start primary
        self.p_proc = subprocess.Popen(
            ["../build/redis_kv_store", "--port", "6379"],
            cwd="primary_dir",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(0.5)

        p_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        p_sock.connect(('127.0.0.1', 6379))
        
        # Write to primary
        send_recv(p_sock, "*3\r\n$3\r\nSET\r\n$1\r\nA\r\n$1\r\n1\r\n")

        # Start replica
        self.r_proc = subprocess.Popen(
            ["../build/redis_kv_store", "--port", "6380", "--replicaof", "127.0.0.1", "6379"],
            cwd="replica_dir",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(1.0) # Wait for sync

        r_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        r_sock.connect(('127.0.0.1', 6380))

        # Test Read-Only
        resp = send_recv(r_sock, "*3\r\n$3\r\nSET\r\n$1\r\nB\r\n$1\r\n2\r\n")
        self.assertTrue("READONLY" in resp)

        # Test A was synced
        resp = send_recv(r_sock, "*2\r\n$3\r\nGET\r\n$1\r\nA\r\n")
        self.assertEqual(resp, "$1\r\n1\r\n")

        # Test streaming
        send_recv(p_sock, "*3\r\n$3\r\nSET\r\n$1\r\nC\r\n$1\r\n3\r\n")
        time.sleep(0.1) # Wait for stream
        resp = send_recv(r_sock, "*2\r\n$3\r\nGET\r\n$1\r\nC\r\n")
        self.assertEqual(resp, "$1\r\n3\r\n")
        
        # Test Disconnect/Reconnect & Offset Continuity
        r_sock.close()
        self.r_proc.terminate()
        self.r_proc.wait()
        
        # Write to primary while replica is dead
        send_recv(p_sock, "*3\r\n$3\r\nSET\r\n$1\r\nD\r\n$1\r\n4\r\n")
        
        # Restart replica (it will send PSYNC <offset>)
        self.r_proc = subprocess.Popen(
            ["../build/redis_kv_store", "--port", "6380", "--replicaof", "127.0.0.1", "6379"],
            cwd="replica_dir",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(1.0)
        
        r_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        r_sock.connect(('127.0.0.1', 6380))
        
        # Verify D was synced via PSYNC offset
        resp = send_recv(r_sock, "*2\r\n$3\r\nGET\r\n$1\r\nD\r\n")
        self.assertEqual(resp, "$1\r\n4\r\n")

        p_sock.close()
        r_sock.close()

if __name__ == '__main__':
    unittest.main()
