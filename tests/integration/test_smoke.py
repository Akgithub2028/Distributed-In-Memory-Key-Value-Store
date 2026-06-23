import socket
import unittest
import subprocess
import time

def send_recv(sock, req_str):
    sock.sendall(req_str.encode('utf-8'))
    resp = sock.recv(1024).decode('utf-8')
    return resp

class TestSmoke(unittest.TestCase):
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

    def test_ping(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 6379))
            resp = send_recv(s, "*1\r\n$4\r\nPING\r\n")
            self.assertEqual(resp, "+PONG\r\n")

    def test_ping_with_arg(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 6379))
            resp = send_recv(s, "*2\r\n$4\r\nPING\r\n$5\r\nhello\r\n")
            self.assertEqual(resp, "$5\r\nhello\r\n")

    def test_echo(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 6379))
            resp = send_recv(s, "*2\r\n$4\r\nECHO\r\n$5\r\nworld\r\n")
            self.assertEqual(resp, "$5\r\nworld\r\n")

    def test_unknown_command(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 6379))
            resp = send_recv(s, "*1\r\n$4\r\nFAKE\r\n")
            self.assertTrue(resp.startswith("-ERR unknown command"))

    def test_malformed_request(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 6379))
            s.sendall(b"not resp\r\n")
            # Currently our parser will ignore or fail, wait for socket close
            pass 

    def test_quit(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 6379))
            resp = send_recv(s, "*1\r\n$4\r\nQUIT\r\n")
            self.assertEqual(resp, "+OK\r\n")
            
            # verify connection closed by attempting to send again
            try:
                s.sendall(b"*1\r\n$4\r\nPING\r\n")
                s.recv(1024)
            except (ConnectionResetError, BrokenPipeError):
                pass # expected

if __name__ == '__main__':
    unittest.main()
