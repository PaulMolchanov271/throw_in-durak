"""Real TCP tests; run from the repository root after building ./server."""
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time

SERVER = str(Path('server').resolve())
class Client:
    def __init__(self):
        self.sock = socket.create_connection(('127.0.0.1', 5555), timeout=2)
        self.buf = b''
    def send(self, text): self.sock.sendall((text+'\n').encode())
    def until(self, prefix, timeout=3):
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            if b'\n' in self.buf:
                line, self.buf = self.buf.split(b'\n', 1)
                text = line.decode()
                if text.startswith(prefix): return text
                continue
            if select.select([self.sock], [], [], max(0, deadline-time.monotonic()))[0]:
                chunk = self.sock.recv(4096)
                assert chunk, 'unexpected disconnect'
                self.buf += chunk
        raise AssertionError(f'timeout waiting for {prefix!r}; buffer={self.buf!r}')
    def signup(self, name):
        # Commands in one TCP write exercise framing and multiple-line handling.
        self.send('1\n'+name+'\nsecret')
        self.until('You have been successfully Sign up')
    def close(self): self.sock.close()

def start(directory, log):
    p = subprocess.Popen([SERVER], cwd=directory, stdout=log, stderr=log)
    for _ in range(100):
        assert p.poll() is None, 'server failed to start'
        try:
            probe = socket.create_connection(('127.0.0.1', 5555), timeout=.1)
            probe.close(); return p
        except OSError: time.sleep(.03)
    raise AssertionError('server startup timeout')

with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile() as log:
    p = start(directory, log)
    clients = []
    try:
        a, b, observer = [Client() for _ in range(3)]
        clients = [a,b,observer]
        a.signup('Alice'); b.signup('Bob'); observer.send('3'); observer.until('Guest')
        a.send('#join -1'); a.until('#error')
        a.send('#create room'); assert a.until('0') == '0'
        a.send('#game_start'); a.until('#error')
        b.send('#join 0'); b.until('#joined')
        a.send('#game_start'); a.until('#game_start'); b.until('#game_start')
        a.send('#chat hello from game'); assert 'hello from game' in b.until('#chat')
        observer.send('#profile'); assert observer.until('#profile').endswith(' 0 0')
        assert b'#chat' not in observer.buf, 'lobby chat leaked'
        b.send('#leave'); b.until('#left'); a.until('#game_end -2')
        a.send('#profile'); assert a.until('#profile').endswith(' 1 0')
        b.send('#profile'); assert b.until('#profile').endswith(' 0 1')
        b.send('#join 0'); b.until('#joined')
        a.send('#restart'); a.until('#game_start'); b.until('#game_start')
        b.close(); a.until('#game_end -2')
        a.send('#profile'); assert a.until('#profile').endswith(' 2 0')
        a.send('#leave'); a.until('#left')
        a.send('#create next'); assert a.until('0') == '0'
        a.send('#leave'); a.until('#left')
        # Oversized frames must disconnect without corrupting the server.
        bad = Client(); bad.sock.sendall(b'x'*5000); bad.close()
        a.send('#profile'); assert a.until('#profile').endswith(' 2 0')
    finally:
        for c in clients: c.close()
        p.terminate(); p.wait(timeout=3)
    p = start(directory, log)
    try:
        a = Client(); a.send('2\nAlice\nsecret'); a.until('You have been successfully logged')
        a.send('#profile'); assert a.until('#profile').endswith(' 2 0')
        a.close()
    finally:
        p.terminate(); p.wait(timeout=3)
    log.seek(0); output = log.read().decode(errors='replace')
    assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output, output
print('TCP chat, profile persistence, restart, disconnect and framing tests passed')
