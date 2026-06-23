import socket

class RESPParserError(Exception):
    pass

class KVClient:
    def __init__(self, host='127.0.0.1', port=6379, timeout=5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.buffer = b""
        
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self.buffer = b""

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def execute_command(self, *args):
        if not self.sock:
            self.connect()
        
        # Serialize command to RESP Array of Bulk Strings
        cmd = f"*{len(args)}\r\n".encode('utf-8')
        for arg in args:
            str_arg = str(arg).encode('utf-8')
            cmd += f"${len(str_arg)}\r\n".encode('utf-8') + str_arg + b"\r\n"
            
        self.sock.sendall(cmd)
        return self._read_response()

    def _read_response(self):
        while b"\r\n" not in self.buffer:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("Server closed connection")
            self.buffer += data
            
        line_end = self.buffer.find(b"\r\n")
        line = self.buffer[:line_end]
        self.buffer = self.buffer[line_end + 2:]
        
        if len(line) == 0:
            raise RESPParserError("Empty line")
            
        resp_type = chr(line[0])
        content = line[1:]
        
        if resp_type == '+': # Simple String
            return content.decode('utf-8')
        elif resp_type == '-': # Error
            raise Exception(content.decode('utf-8'))
        elif resp_type == ':': # Integer
            return int(content)
        elif resp_type == '$': # Bulk String
            length = int(content)
            if length == -1:
                return None
            
            # Read until we have enough data for the string + \r\n
            while len(self.buffer) < length + 2:
                data = self.sock.recv(4096)
                if not data:
                    raise ConnectionError("Server closed connection")
                self.buffer += data
                
            string_data = self.buffer[:length]
            self.buffer = self.buffer[length + 2:] # skip \r\n
            return string_data.decode('utf-8')
        elif resp_type == '*': # Array
            length = int(content)
            if length == -1:
                return None
            return [self._read_response() for _ in range(length)]
        else:
            raise RESPParserError(f"Unknown RESP type: {resp_type}")

    # Convenience Methods
    def get(self, key):
        return self.execute_command("GET", key)

    def set(self, key, value):
        return self.execute_command("SET", key, value)

    def delete(self, *keys):
        return self.execute_command("DEL", *keys)

    def exists(self, *keys):
        return self.execute_command("EXISTS", *keys)
        
    def incr(self, key):
        return self.execute_command("INCR", key)

    def ping(self, msg=None):
        if msg:
            return self.execute_command("PING", msg)
        return self.execute_command("PING")

    def info(self, section=None):
        if section:
            return self.execute_command("INFO", section)
        return self.execute_command("INFO")
