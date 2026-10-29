#!/usr/bin/env python3
"""Protocol-level test client for the web-panel bridge.

Connects to bridge.py's WebSocket and sends the exact JSON messages
bridge.js sends, then watches for the feedback messages the bridge
emits. This verifies the bridge at the same level the page uses it,
without a browser.

Usage:
    python3 test_client.py pad      # pad 1 press/release (voice evidence is
                                    # read from the rig GUI, not from WS)
    python3 test_client.py browse   # BROWSE press; expect a BROWSE led-on msg
    python3 test_client.py all      # both, sequentially
Exit code 0 = expectations met, 1 = not.
"""

import base64
import json
import os
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bridge import WSClient  # reuse the framing code


def connect(port=8794):
    raw = socket.create_connection(("127.0.0.1", port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    raw.sendall((f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                 f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = raw.recv(4096)
        if not chunk:
            raise ConnectionError("handshake closed")
        resp += chunk
    if b"101" not in resp.split(b"\r\n")[0]:
        raise ConnectionError(f"handshake failed: {resp[:80]}")

    client = WSClient(raw, ("test", 0))

    # Client->server frames MUST be masked (RFC 6455); WSClient.send_text
    # writes server-style unmasked frames, so wrap it here.
    def masked_send(text):
        payload = text.encode()
        mask = os.urandom(4)
        header = bytearray([0x81])
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126); header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127); header += struct.pack(">Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        with client.send_lock:
            client.conn.sendall(bytes(header) + mask + masked)

    client.send_text = masked_send
    return client


class Inbox:
    def __init__(self, client):
        self.messages = []
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, args=(client,), daemon=True)
        self.thread.start()

    def _run(self, client):
        try:
            for text in client.frames():
                try:
                    msg = json.loads(text)
                except ValueError:
                    continue
                with self.lock:
                    self.messages.append(msg)
        except Exception:
            pass

    def snapshot(self):
        with self.lock:
            return list(self.messages)

    def clear(self):
        with self.lock:
            self.messages.clear()


def wait_for(inbox, pred, timeout=3.0):
    end = time.time() + timeout
    while time.time() < end:
        hits = [m for m in inbox.snapshot() if pred(m)]
        if hits:
            return hits
        time.sleep(0.05)
    return []


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    client = connect()
    inbox = Inbox(client)
    # Note: the bridge's {"t":"ready"} greeting can be swallowed by this
    # client's naive handshake reader (bytes after the HTTP headers are
    # dropped). The page's real WebSocket implementation is unaffected;
    # don't gate on it here.
    ready = wait_for(inbox, lambda m: m.get("t") == "ready", 2.0)
    print(f"connected (bridge greeting seen: {bool(ready)} — informational only)")
    ok = True

    if mode in ("pad", "all"):
        inbox.clear()
        client.send_text(json.dumps({"t": "pad", "pad": 1, "vel": 127, "down": True}))
        time.sleep(0.4)
        client.send_text(json.dumps({"t": "pad", "pad": 1, "vel": 0, "down": False}))
        time.sleep(0.5)
        print(f"pad 1 press+release sent. feedback meanwhile: {inbox.snapshot()}")
        print("  (voice evidence is read from the rig GUI meters/logs, not WS)")

    if mode in ("browse", "all"):
        inbox.clear()
        client.send_text(json.dumps({"t": "button", "id": "BROWSE", "down": True}))
        leds = wait_for(inbox,
                        lambda m: m.get("t") == "led" and m.get("id") == "BROWSE" and m.get("on"),
                        timeout=4.0)
        client.send_text(json.dumps({"t": "button", "id": "BROWSE", "down": False}))
        print(f"BROWSE sent. messages: {inbox.snapshot()}")
        if leds:
            print("PASS: bridge emitted BROWSE led on")
        else:
            print("FAIL: no BROWSE led-on message from bridge")
            ok = False

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
