import socket
import unittest
import time
import subprocess
import os

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

class TestPersistence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # We assume the AOF file is in the build directory or cwd.
        # Clean it up before starting the persistence tests.
        if os.path.exists("appendonly.aof"):
            os.remove("appendonly.aof")

    def start_server(self):
        self.server_proc = subprocess.Popen(
            ["./build/redis_kv_store"], 
            stdout=subprocess.DEVNULL, 
            stderr=subprocess.DEVNULL
        )
        time.sleep(1.0) # Wait for it to bind and recover AOF
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(('127.0.0.1', 6379))

    def stop_server(self):
        self.sock.close()
        self.server_proc.terminate()
        self.server_proc.wait()

    def test_basic_persistence(self):
        self.start_server()
        
        # Insert data
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$6\r\nperkey\r\n$3\r\nfoo\r\n")
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$6\r\npernum\r\n$1\r\n5\r\n")
        send_recv(self.sock, "*2\r\n$4\r\nINCR\r\n$6\r\npernum\r\n")
        
        # Verify
        r1 = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$6\r\nperkey\r\n")
        self.assertEqual(r1, "$3\r\nfoo\r\n")
        r2 = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$6\r\npernum\r\n")
        self.assertEqual(r2, "$1\r\n6\r\n")
        
        # Kill server
        self.stop_server()
        
        # Restart server
        self.start_server()
        
        # Verify data survived
        r1 = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$6\r\nperkey\r\n")
        self.assertEqual(r1, "$3\r\nfoo\r\n")
        r2 = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$6\r\npernum\r\n")
        self.assertEqual(r2, "$1\r\n6\r\n")
        
        self.stop_server()

    def test_ttl_persistence(self):
        self.start_server()
        
        # Set a key that expires in 4 seconds
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$6\r\nttlkey\r\n$3\r\nbar\r\n")
        send_recv(self.sock, "*3\r\n$6\r\nEXPIRE\r\n$6\r\nttlkey\r\n$1\r\n4\r\n")
        
        # Verify it has TTL
        r1 = send_recv(self.sock, "*2\r\n$3\r\nTTL\r\n$6\r\nttlkey\r\n")
        self.assertTrue(r1 == ":4\r\n" or r1 == ":3\r\n") # could be 3 or 4 depending on exact ms
        
        self.stop_server()
        
        # Wait 1.5 seconds while server is dead
        time.sleep(1.5)
        
        # Restart
        self.start_server()
        
        # Check PTTL, should be roughly ~1500ms left (4000 - 1500 - 1000)
        resp = send_recv(self.sock, "*2\r\n$4\r\nPTTL\r\n$6\r\nttlkey\r\n")
        # Extract integer
        print("PTTL RESP:", repr(resp))
        pttl = int(resp.strip()[1:])
        self.assertTrue(pttl > 500 and pttl < 2500, f"PTTL was {pttl}")
        
        # Wait another 3 seconds
        time.sleep(3.0)
        
        # Should be gone
        resp = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$6\r\nttlkey\r\n")
        self.assertEqual(resp, "$-1\r\n")
        
        self.stop_server()

if __name__ == '__main__':
    unittest.main()
