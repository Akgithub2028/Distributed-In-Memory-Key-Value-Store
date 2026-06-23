import socket
import unittest
import time
import subprocess

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

class TestTTL(unittest.TestCase):
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
        # Flush DB equivalent: DEL known test keys
        send_recv(self.sock, "*2\r\n$3\r\nDEL\r\n$4\r\ntttl\r\n")

    def tearDown(self):
        self.sock.close()

    def test_missing_ttl(self):
        resp = send_recv(self.sock, "*2\r\n$3\r\nTTL\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, ":-2\r\n")

    def test_no_expire_ttl(self):
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntttl\r\n$3\r\nval\r\n")
        resp = send_recv(self.sock, "*2\r\n$3\r\nTTL\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, ":-1\r\n")

    def test_expire(self):
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntttl\r\n$3\r\nval\r\n")
        
        # PEXPIRE 100ms
        resp = send_recv(self.sock, "*3\r\n$7\r\nPEXPIRE\r\n$4\r\ntttl\r\n$3\r\n100\r\n")
        self.assertEqual(resp, ":1\r\n")
        
        # Should exist immediately
        resp = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, "$3\r\nval\r\n")
        
        # Wait 200ms
        time.sleep(0.2)
        
        # Should be gone
        resp = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, "$-1\r\n")

    def test_persist(self):
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntttl\r\n$3\r\nval\r\n")
        
        # EXPIRE 2 seconds
        send_recv(self.sock, "*3\r\n$6\r\nEXPIRE\r\n$4\r\ntttl\r\n$1\r\n2\r\n")
        
        # PERSIST
        resp = send_recv(self.sock, "*2\r\n$7\r\nPERSIST\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, ":1\r\n")
        
        # TTL should be -1
        resp = send_recv(self.sock, "*2\r\n$3\r\nTTL\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, ":-1\r\n")

    def test_set_clears_ttl(self):
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntttl\r\n$3\r\nval\r\n")
        send_recv(self.sock, "*3\r\n$6\r\nEXPIRE\r\n$4\r\ntttl\r\n$3\r\n100\r\n")
        
        # Overwrite with SET
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntttl\r\n$4\r\nnewv\r\n")
        
        # TTL should be -1
        resp = send_recv(self.sock, "*2\r\n$3\r\nTTL\r\n$4\r\ntttl\r\n")
        self.assertEqual(resp, ":-1\r\n")

if __name__ == '__main__':
    unittest.main()
