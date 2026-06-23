import socket
import unittest
import threading
import subprocess
import time

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

class TestCRUD(unittest.TestCase):
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
        # Flush DB equivalent: DEL all known test keys just to be safe between runs
        send_recv(self.sock, "*3\r\n$3\r\nDEL\r\n$4\r\ntest\r\n$6\r\nnumber\r\n")

    def tearDown(self):
        self.sock.close()

    def test_get_missing(self):
        resp = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$7\r\nmissing\r\n")
        self.assertEqual(resp, "$-1\r\n")

    def test_set_and_get(self):
        resp = send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntest\r\n$5\r\nhello\r\n")
        self.assertEqual(resp, "+OK\r\n")
        
        resp = send_recv(self.sock, "*2\r\n$3\r\nGET\r\n$4\r\ntest\r\n")
        self.assertEqual(resp, "$5\r\nhello\r\n")

    def test_exists_and_del(self):
        # SET key
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$4\r\ntest\r\n$5\r\nhello\r\n")
        
        # EXISTS test -> 1
        resp = send_recv(self.sock, "*2\r\n$6\r\nEXISTS\r\n$4\r\ntest\r\n")
        self.assertEqual(resp, ":1\r\n")
        
        # DEL test -> 1
        resp = send_recv(self.sock, "*2\r\n$3\r\nDEL\r\n$4\r\ntest\r\n")
        self.assertEqual(resp, ":1\r\n")
        
        # EXISTS test -> 0
        resp = send_recv(self.sock, "*2\r\n$6\r\nEXISTS\r\n$4\r\ntest\r\n")
        self.assertEqual(resp, ":0\r\n")

    def test_incr_decr(self):
        # INCR non-existent -> 1
        resp = send_recv(self.sock, "*2\r\n$4\r\nINCR\r\n$6\r\nnumber\r\n")
        self.assertEqual(resp, ":1\r\n")
        
        # INCR -> 2
        resp = send_recv(self.sock, "*2\r\n$4\r\nINCR\r\n$6\r\nnumber\r\n")
        self.assertEqual(resp, ":2\r\n")
        
        # DECR -> 1
        resp = send_recv(self.sock, "*2\r\n$4\r\nDECR\r\n$6\r\nnumber\r\n")
        self.assertEqual(resp, ":1\r\n")
        
        # SET string
        send_recv(self.sock, "*3\r\n$3\r\nSET\r\n$3\r\nstr\r\n$3\r\nfoo\r\n")
        # INCR str -> error
        resp = send_recv(self.sock, "*2\r\n$4\r\nINCR\r\n$3\r\nstr\r\n")
        self.assertEqual(resp, "-ERR value is not an integer or out of range\r\n")

    def test_concurrent_access(self):
        def worker(idx):
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.connect(('127.0.0.1', 6379))
                key = f"key_{idx}"
                send_recv(s, f"*3\r\n$3\r\nSET\r\n${len(key)}\r\n{key}\r\n$4\r\ndata\r\n")
                resp = send_recv(s, f"*2\r\n$3\r\nGET\r\n${len(key)}\r\n{key}\r\n")
                assert resp == "$4\r\ndata\r\n"

        threads = []
        for i in range(20):
            t = threading.Thread(target=worker, args=(i,))
            threads.append(t)
            t.start()
            
        for t in threads:
            t.join()
            
if __name__ == '__main__':
    unittest.main()
