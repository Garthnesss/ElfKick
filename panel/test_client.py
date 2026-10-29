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
    python3 test_client.py stream [host] [seconds]
                                    # subscribe to the screen stream; count
                                    # binary JPEG frames, validate them, save
                                    # first/last to ../panel-stream-frame*.jpg
                                    # (+ .png via ImageMagick when available)
Exit code 0 = expectations met, 1 = not.
"""

import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bridge import WSClient  # reuse the framing code


def connect(port=8794, host="127.0.0.1"):
    raw = socket.create_connection((host, port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    raw.sendall((f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\n"
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


class RawInbox:
    """Reader that keeps text (JSON) messages and binary frames apart —
    the screen stream travels as binary WebSocket frames."""

    def __init__(self, client):
        self.texts = []
        self.frames = []               # (arrival time, bytes)
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, args=(client,), daemon=True)
        self.thread.start()

    def _run(self, client):
        try:
            for opcode, payload in client.frames_raw():
                with self.lock:
                    if opcode == 0x2:
                        self.frames.append((time.time(), payload))
                    else:
                        try:
                            self.texts.append(json.loads(payload.decode("utf-8", "replace")))
                        except ValueError:
                            pass
        except Exception:
            pass

    def snapshot_texts(self):
        with self.lock:
            return list(self.texts)

    def snapshot_frames(self):
        with self.lock:
            return list(self.frames)


def jpeg_dims(data: bytes):
    """(width, height) from the first SOF marker, or None."""
    if not data.startswith(b"\xff\xd8"):
        return None
    i = 2
    while i + 4 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7 or marker == 0x01:
            i += 2
            continue
        length = struct.unpack(">H", data[i + 2:i + 4])[0]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            return (w, h)
        i += 2 + length
    return None


def save_frame(data: bytes, basename: str):
    """Save a raw JPEG frame next to panel/ (i.e. in ~/workspace/elfkick),
    plus a true-PNG copy via ImageMagick when available."""
    out_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    jpg_path = os.path.join(out_dir, basename + ".jpg")
    with open(jpg_path, "wb") as f:
        f.write(data)
    paths = [jpg_path]
    if shutil.which("convert"):
        png_path = os.path.join(out_dir, basename + ".png")
        try:
            subprocess.run(["convert", jpg_path, png_path],
                           check=True, timeout=10, capture_output=True)
            paths.append(png_path)
        except (OSError, subprocess.SubprocessError):
            pass
    return paths


def stream_mode(host, seconds):
    client = connect(host=host)
    inbox = RawInbox(client)
    client.send_text(json.dumps({"t": "stream", "on": True}))
    ack = []
    end = time.time() + 4.0
    while time.time() < end and not ack:
        ack = [m for m in inbox.snapshot_texts()
               if m.get("t") == "stream" and m.get("on")]
        time.sleep(0.05)
    if not ack:
        print("FAIL: bridge never confirmed the stream subscription")
        return False
    print(f"stream confirmed by bridge: {ack[0]}")
    t0 = time.time()
    time.sleep(seconds)
    frames = inbox.snapshot_frames()
    client.send_text(json.dumps({"t": "stream", "on": False}))
    dt = time.time() - t0
    frames = [(t, b) for (t, b) in frames if t0 - 1.0 <= t <= t0 + dt + 0.5]
    if not frames:
        print("FAIL: no binary frames received")
        return False
    bad = [b for _, b in frames
           if not (b.startswith(b"\xff\xd8") and b.endswith(b"\xff\xd9"))]
    dims = jpeg_dims(frames[0][1])
    sizes = [len(b) for _, b in frames]
    print(f"received {len(frames)} frames in {dt:.1f}s "
          f"= {len(frames) / dt:.2f} fps; frame size min {min(sizes)} / "
          f"avg {sum(sizes) // len(sizes)} / max {max(sizes)} B; "
          f"first frame dims {dims}; corrupt frames: {len(bad)}")
    for p in save_frame(frames[0][1], "panel-stream-frame"):
        print(f"saved first frame: {p}")
    if len(frames) > 1:
        for p in save_frame(frames[-1][1], "panel-stream-frame-last"):
            print(f"saved last frame: {p}")
    ok = len(frames) >= 10 and not bad and dims == (1024, 600)
    print("PASS" if ok else "FAIL")
    return ok


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    if mode == "stream":
        seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 5.0
        sys.exit(0 if stream_mode(host, seconds) else 1)
    client = connect(host=host)
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
