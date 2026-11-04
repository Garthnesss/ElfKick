#!/usr/bin/env python3
"""ElfKick web-panel bridge — plays the KICK's hardware MCU.

Owns a pty: /dev/ttymxc0 is symlinked to the slave end, so the real
KickDeviceControl process opens it exactly as it would the device UART
(230400 8N1). The bridge speaks ComProtocol/PktMgr framing on the
master end — web-panel events in, LED/pad feedback out — and bridges
to the browser over a hand-rolled WebSocket (stdlib only, no deps).
It also serves the panel directory over HTTP.

Design + mapping: panel/BRIDGE.md.

Usage:
    python3 bridge.py [--host 127.0.0.1] [--ws-port 8794]
                      [--http-port 8793] [--no-http] [--link /dev/ttymxc0]
                      [--stream-fps 5] [--stream-quality 60]
                      [--display :99]

Screen streaming: a WS client may send {"t":"stream","on":true} to
subscribe to live frames of the Kick GUI window (X display --display).
The bridge answers {"t":"stream","on":true,"w":1024,"h":600,"fps":N}
and then sends the frames as BINARY WebSocket messages (raw JPEG,
1024x600). Capture runs only while at least one client is subscribed.
Design: panel/BRIDGE.md.

Post-input burst (Oct 30, 2026, fourth addendum): after any panel
input that should change the device screen, the streamer drops its
inter-frame pacing for ~1 s and captures back-to-back, so the change
reaches the page at capture speed instead of poll speed. Repeated
inputs extend the burst but a burst session is capped at 3 s, so
knob drags cannot keep the capturer pinned.

Health snapshot (same addendum): the bridge owns the rig-health
facts and pushes {"t":"state", ...} to every WS client on connect,
on request ({"t":"state"}), and every 2 s; the same snapshot is
served over HTTP at GET /state.json. The page chrome renders it as
health dots (bridge / DCL link / engine / screen / input).
"""

import argparse
import base64
import collections
import hashlib
import json
import os
import pty
import socket
import struct
import subprocess
import sys
import termios
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

# ---------------------------------------------------------------------------
# PktMgr codec (ComProtocol/PktMgr/PktMgr.cpp)
# Frame: 'M' 'T' type dir cmd size data[size-1] crc8 ; size = len(data)+1
# CRC-8: init 0xFF, poly 0x31, over all preceding bytes.
# ---------------------------------------------------------------------------

PKT_SYSTEM, PKT_BUTTON, PKT_LED, PKT_PAD, PKT_KNOBS, PKT_MIDI, PKT_CV, PKT_SLIDER = range(8)
DIR_REQUEST, DIR_RESPONSE = 0, 1

TYPE_NAMES = {0: "SYSTEM", 1: "BUTTON", 2: "LED", 3: "PAD", 4: "KNOBS",
              5: "MIDI", 6: "CV", 7: "SLIDER"}


def pkt_crc(data: bytes) -> int:
    crc = 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def build_frame(pkt_type: int, pkt_dir: int, cmd: int, data) -> bytes:
    data = bytes(bytearray(data))  # ints -> bytes
    body = b"MT" + bytes([pkt_type, pkt_dir, cmd, len(data) + 1]) + data
    return body + bytes([pkt_crc(body)])


class FrameParser:
    """Streaming parser mirroring CPktMgr::PushByteToRxFifo semantics:
    resync on 'M','T'; a frame is yielded only when its CRC checks out."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, chunk: bytes):
        self.buf += chunk
        frames = []
        while True:
            # resync to header
            start = self.buf.find(b"MT")
            if start == -1:
                # keep a trailing 'M' — could be half a header
                keep = 1 if self.buf.endswith(b"M") else 0
                del self.buf[: len(self.buf) - keep]
                break
            if start:
                del self.buf[:start]
            if len(self.buf) < 6:
                break
            size = self.buf[5]
            # Wire length of a PktMgr frame: 5 header bytes + size byte
            # + (size-1) data bytes + CRC = size + 6.
            total = size + 6
            if len(self.buf) < total:
                break
            frame = bytes(self.buf[:total])
            del self.buf[:total]
            if pkt_crc(frame[:-1]) != frame[-1]:
                log(f"pty rx: CRC mismatch, dropping frame {frame.hex()}")
                continue
            frames.append((frame[2], frame[3], frame[4], list(frame[6:-1])))
        return frames


# ---------------------------------------------------------------------------
# Contracts tables (Kick/modules/contracts/dcl/dcl_contracts.h)
# ---------------------------------------------------------------------------

# BtnLedSequence code -> web panel button id. Shift-function codes fold
# onto the physical button that carries the label (panel MPC.setLed ids).
LED_CODE_TO_PANEL = {
    0: "A", 1: "B", 2: "C", 3: "D",
    4: "A", 5: "B", 6: "C", 7: "D",            # BANK_E..H fold to A..D
    8: "MAIN", 9: "MAIN",                      # MAINMODE / GRID
    10: "BROWSE", 11: "BROWSE",                # BROWSE / SAVE
    12: "TRACK_MIX", 13: "TRACK_MIX",          # TRACK_MIX / PAD_MIX
    14: "TRACK_MUTE", 15: "TRACK_MUTE",        # TRACK_MUTE / PAD_MUTE
    16: "NEXT_SEQ", 17: "NEXT_SEQ",            # NEXT_SEQ / FX
    18: "TAP_TEMPO", 19: "TAP_TEMPO",          # TAP_TEMPO / MASTER
    22: "MENU", 23: "MENU",                    # MENU / PREF
    24: "NOTE_REPEAT", 25: "NOTE_REPEAT",      # NOTE_REPEAT / LATCH
    31: "SHIFT",
    32: "STEP_SEQ", 33: "STEP_SEQ",            # STEP_SEQ / AUTOMATION
    34: "TC", 35: "TC",                        # TC / ON_OFF
    36: "SAMPLER", 37: "SAMPLER",              # SAMPLER / LOOPER
    38: "SAMPLE_EDIT", 39: "SAMPLE_EDIT",      # SAMPLE_EDIT / LINK_EDIT
    40: "PROG_EDIT", 41: "PROG_EDIT",          # PROG_EDIT / MIDI_CONTROL
    42: "REC", 43: "OVERDUB", 44: "STOP", 45: "PLAY", 46: "PLAY_START",
    48: "FULL_LEVEL", 49: "FULL_LEVEL",        # FULL_LEVEL / HALF_LEVEL
    50: "16_LEVELS", 52: "ERASE",
    # no LED for: 20 COPY, 21 PLUS, 27 MINUS, 28 KNOB_CCW, 29 BACK,
    # 30 KNOB_CW, 47 LINK, 51 NOTES, 54 COLOR_EDIT — and 26 KNOB_ENTER,
    # which has no LED either but DOES have a panel event since Oct 30:
    # the jog push (see the "jogpush" handler in on_web_message).
}

# Panel button id -> BtnLedSequence code (unshifted), and the shifted
# code where the silkscreen carries a shift function. Mirrors
# kick_SimulatorMainControl.cpp (the in-tree MCU-behaviour reference).
BUTTON_CODES = {
    "A": (0, 4), "B": (1, 5), "C": (2, 6), "D": (3, 7),
    "MAIN": (8, 9), "BROWSE": (10, 11), "TRACK_MIX": (12, 13),
    "TRACK_MUTE": (14, 15), "NEXT_SEQ": (16, 17), "TAP_TEMPO": (18, 19),
    "MENU": (22, 23), "NOTE_REPEAT": (24, 25), "STEP_SEQ": (32, 33),
    "TC": (34, 35), "SAMPLER": (36, 37), "SAMPLE_EDIT": (38, 39),
    "PROG_EDIT": (40, 41),
    "SHIFT": (31, 31), "FULL_LEVEL": (48, 48), "16_LEVELS": (50, 50),
    "ERASE": (52, 52), "REC": (42, 42), "OVERDUB": (43, 43),
    "STOP": (44, 44), "PLAY": (45, 45), "PLAY_START": (46, 46),
}
EN_PLUS, EN_MINUS, EN_KNOB_ENTER, EN_KNOB_CW, EN_KNOB_CCW, EN_SHIFT = \
    21, 27, 26, 30, 28, 31

# PadColourIds -> CSS approximations (panel speaks CSS; null = pad's own)
PAD_COLOURS = {
    0: "#8b0000", 1: "#bf40ff", 2: "#b8860b", 3: "#90ee90", 4: "#66cc66",
    5: "#33aa55", 6: "#87cefa", 7: "#00ffff", 8: "#3366ff", 9: "#ffc0cb",
    10: "#ff99aa", 11: "#ff6699", 12: "#ff0000", 13: "#ff8800", 14: "#ffa500",
    15: None, 16: "#f2f2f2", 17: "#888888",
}
# PadButtonState -> MPC.setPad level
PAD_STATE_LEVEL = {0: 0.0, 1: 1.0, 2: 1.0, 3: 0.35, 4: 0.7, 5: 0.3}

KNOB_CODES = {"P1": 0, "P2": 1, "P3": 2, "P4": 3, "MASTER": 4, "REC": 5}
KNOB_DEFAULTS = {"MASTER": 0.8, "REC": 0.5, "P1": 0.5, "P2": 0.5, "P3": 1.0, "P4": 0.0}

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

_log_lock = threading.Lock()


def log(msg: str):
    with _log_lock:
        print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def frame_str(t, d, c, data):
    return f"{TYPE_NAMES.get(t, t)}/{c} dir={d} data={data}"


# ---------------------------------------------------------------------------
# Minimal WebSocket server (RFC 6455, text frames, stdlib only)
# ---------------------------------------------------------------------------

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WSClient:
    def __init__(self, conn: socket.socket, addr):
        self.conn = conn
        self.addr = addr
        self.send_lock = threading.Lock()
        self.alive = True
        self.wants_stream = False     # screen-stream subscription (per client)

    def _send_frame(self, opcode: int, payload: bytes):
        header = bytearray([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header.append(n)
        elif n < 65536:
            header.append(126); header += struct.pack(">H", n)
        else:
            header.append(127); header += struct.pack(">Q", n)
        with self.send_lock:
            try:
                self.conn.sendall(bytes(header) + payload)
            except OSError:
                self.alive = False

    def send_text(self, text: str):
        self._send_frame(0x1, text.encode())

    def send_binary(self, data: bytes):
        self._send_frame(0x2, data)

    def read_exact(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self.conn.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("closed")
            buf += chunk
        return buf

    def frames_raw(self):
        """Yield (opcode, payload bytes) until the connection dies.
        Ping is answered in-band; close ends the generator."""
        while True:
            h = self.read_exact(2)
            fin_op, b2 = h[0], h[1]
            opcode = fin_op & 0x0F
            masked = b2 & 0x80
            length = b2 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self.read_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self.read_exact(8))[0]
            mask = self.read_exact(4) if masked else b"\0" * 4
            payload = bytearray(self.read_exact(length)) if length else bytearray()
            for i in range(len(payload)):
                payload[i] ^= mask[i % 4]
            if opcode == 0x8:      # close
                return
            if opcode == 0x9:      # ping -> pong
                with self.send_lock:
                    try:
                        self.conn.sendall(bytes([0x8A, 0]) )
                    except OSError:
                        return
                continue
            if opcode in (0x1, 0x2):
                yield opcode, bytes(payload)

    def frames(self):
        """Yield text payloads until the connection dies."""
        for opcode, payload in self.frames_raw():
            if opcode == 0x1:
                yield payload.decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# Screen streaming — the Kick GUI window, as JPEG frames over the same WS
# ---------------------------------------------------------------------------

STREAM_W, STREAM_H = 1024, 600        # the device touchscreen region


class ScreenStreamer:
    """Captures the KickUserInterface window from the X display and
    pushes JPEG frames to subscribed WS clients.

    Capture is a per-frame ImageMagick `import` subprocess (the tool
    every runtime verification in this repo already used): read the
    fullscreen window, crop the 1024x600 device region at its top-left,
    encode JPEG to stdout. Measured-first numbers on this VM: ~0.21 s
    wall per capture sustained, so the default 5 fps target runs the
    capturer nearly back-to-back; frames land at whatever rate the box
    sustains and the streamer logs the achieved rate.

    The thread idles unless at least one client holds a stream
    subscription — no subscriber, no capture, no CPU.
    """

    def __init__(self, bridge, display=":99", fps=5, quality=60):
        self.bridge = bridge
        self.display = display
        self.period = 1.0 / max(1, fps)
        self.target_fps = fps
        self.quality = quality
        self.window_id = None
        self._stats_t0 = None
        self._stats_frames = 0
        self._stats_bytes = 0
        # Health/burst bookkeeping (fourth addendum): frame timestamps
        # feed the snapshot's fps + last-frame age; burst counters let
        # the log prove a burst actually happened.
        self.frame_times = collections.deque(maxlen=120)
        self.last_frame_t = None
        self.frames_sent = 0
        self.burst_frames = 0
        self._was_bursting = False
        self._burst_session_frames = 0

    # -- health views ----------------------------------------------------
    def fps(self, now=None):
        """Achieved frame rate over the last ~10 s of sent frames."""
        now = now or time.time()
        ts = [t for t in self.frame_times if now - t <= 10.0]
        if len(ts) < 2 or ts[-1] <= ts[0]:
            return 0.0
        return (len(ts) - 1) / (ts[-1] - ts[0])

    def last_frame_age(self, now=None):
        if self.last_frame_t is None:
            return None
        return (now or time.time()) - self.last_frame_t

    # -- window discovery ----------------------------------------------------
    def _env(self):
        env = dict(os.environ)
        env["DISPLAY"] = self.display
        return env

    def _resolve_window(self):
        try:
            out = subprocess.run(
                ["xdotool", "search", "--onlyvisible", "--name",
                 "^KickUserInterface$"],
                capture_output=True, text=True, timeout=5, env=self._env())
            ids = [int(x) for x in out.stdout.split()]
        except (OSError, subprocess.TimeoutExpired, ValueError):
            ids = []
        self.window_id = ids[0] if ids else None
        if self.window_id:
            log(f"stream: GUI window resolved: id {self.window_id} on {self.display}")
        else:
            log("stream: KickUserInterface window not found (yet)")
        return self.window_id

    # -- capture ---------------------------------------------------------------
    def _capture(self):
        if self.window_id is None and self._resolve_window() is None:
            return None
        try:
            out = subprocess.run(
                ["import", "-window", str(self.window_id),
                 "-crop", f"{STREAM_W}x{STREAM_H}+0+0", "+repage",
                 "-quality", str(self.quality), "jpg:-"],
                capture_output=True, timeout=5, env=self._env())
        except (OSError, subprocess.TimeoutExpired):
            self.window_id = None
            return None
        data = out.stdout
        if out.returncode != 0 or not data.startswith(b"\xff\xd8"):
            # window may have been restarted under a new id — re-resolve
            log("stream: capture failed, will re-resolve window")
            self.window_id = None
            return None
        return data

    # -- main loop ---------------------------------------------------------------
    def run(self):
        while True:
            if not self.bridge.stream_subscribers():
                if self._stats_t0 is not None:
                    self._flush_stats("stopped")
                time.sleep(0.25)
                continue
            if self._stats_t0 is None:
                self._stats_t0 = time.time()
                self._stats_frames = 0
                self._stats_bytes = 0
                log(f"stream: capture started (target {self.target_fps} fps, "
                    f"JPEG q{self.quality}, {STREAM_W}x{STREAM_H})")
            t0 = time.time()
            data = self._capture()
            bursting = t0 < self.bridge.burst_until
            if data:
                self.bridge.send_frame(data)
                now = time.time()
                self.frame_times.append(now)
                self.last_frame_t = now
                self.frames_sent += 1
                self._stats_frames += 1
                self._stats_bytes += len(data)
                if bursting:
                    self.burst_frames += 1
                    self._burst_session_frames += 1
                if time.time() - self._stats_t0 >= 15:
                    self._flush_stats("running")
            if bursting and not self._was_bursting:
                log("stream: burst started (post-input, back-to-back capture)")
            if self._was_bursting and not bursting:
                log(f"stream: burst ended ({self._burst_session_frames} "
                    f"burst frames this session)")
                self._burst_session_frames = 0
            self._was_bursting = bursting
            dt = time.time() - t0
            if data is None:
                # Failed capture (no window yet / window churn): never
                # spin, burst or not — the window resolver needs air.
                time.sleep(self.period)
            elif bursting:
                pass                    # back-to-back: capture speed is the pace
            elif dt < self.period:
                time.sleep(self.period - dt)

    def _flush_stats(self, why):
        if self._stats_t0 is None:
            return
        dt = time.time() - self._stats_t0
        if dt > 0 and self._stats_frames:
            log(f"stream: {why}: {self._stats_frames} frames in {dt:.1f}s "
                f"= {self._stats_frames / dt:.2f} fps, "
                f"avg {self._stats_bytes // max(1, self._stats_frames)} B/frame")
        self._stats_t0 = None
        self._stats_frames = 0
        self._stats_bytes = 0


# ---------------------------------------------------------------------------
# The bridge
# ---------------------------------------------------------------------------

class Bridge:
    def __init__(self, link_path: str, stream_fps=5, stream_quality=60,
                 stream_display=":99"):
        self.link_path = link_path
        self.master_fd = None
        self.parser = FrameParser()
        self.pty_lock = threading.Lock()
        self.clients = []
        self.clients_lock = threading.Lock()
        self.shift_held = False
        self.emitted_code = {}          # panel button id -> code actually sent (for release pairing)
        self.knob_last = dict(KNOB_DEFAULTS)
        self.announced = False
        self.streamer = ScreenStreamer(self, display=stream_display,
                                       fps=stream_fps, quality=stream_quality)
        # Health snapshot + burst state (fourth addendum).
        self.started_t = time.time()
        self.last_hw_rx_t = None        # last byte chunk seen from DCL
        self.last_input_t = None        # last control input from the page
        self.last_input_kind = None
        self.burst_until = 0.0          # streamer captures back-to-back until then
        self._burst_session_start = 0.0
        self._state_seq = 0
        self._state_lock = threading.Lock()
        self._last_state_push_t = 0.0

    # -- burst -----------------------------------------------------------
    BURST_SPAN = 1.0        # seconds of back-to-back capture per input
    BURST_CAP = 3.0         # a burst session never exceeds this, however
                            # many inputs arrive inside it (coalescing)

    def trigger_burst(self, why: str):
        now = time.time()
        if now > self._burst_session_start + self.BURST_CAP \
                or self._burst_session_start == 0.0:
            self._burst_session_start = now
        self.burst_until = min(now + self.BURST_SPAN,
                               self._burst_session_start + self.BURST_CAP)

    def note_input(self, kind: str):
        """Every real control input from the page funnels through here:
        it feeds the snapshot's input age and fires the post-input
        screen burst."""
        self.last_input_t = time.time()
        self.last_input_kind = kind
        self.trigger_burst(kind)
        self.push_state(min_interval=0.9)

    # -- health snapshot ---------------------------------------------------
    RIG_PROCESSES = ("KickMain", "KickDeviceControl", "KickUserInterface",
                     "KickAudioEngine", "KickAudioModel")

    def _proc_scan(self):
        """(rig process name -> count, DCL holds the pty slave open?).

        Reads /proc directly — the same evidence `ps` would give, with
        no subprocess. The port check resolves our /dev/ttymxc0 symlink
        to its pts and looks for any process holding that fd: that is
        the difference between 'DCL is on the wire' and 'the wire is
        dangling', which the snapshot's DCL dot reports honestly.
        """
        procs = {name: 0 for name in self.RIG_PROCESSES}
        try:
            pts = os.path.realpath(self.link_path)
        except OSError:
            pts = None
        port_open = False
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                with open(f"/proc/{pid}/cmdline", "rb") as f:
                    argv0 = f.read().split(b"\0", 1)[0].decode("utf-8", "replace")
                name = os.path.basename(argv0)
                if name in procs:
                    procs[name] += 1
            except OSError:
                pass
            if pts and not port_open:
                try:
                    for fd in os.listdir(f"/proc/{pid}/fd"):
                        try:
                            if os.readlink(f"/proc/{pid}/fd/{fd}") == pts:
                                port_open = True
                                break
                        except OSError:
                            continue
                except OSError:
                    pass
        return procs, port_open

    def state_snapshot(self):
        now = time.time()
        procs, port_open = self._proc_scan()
        rx_age = None if self.last_hw_rx_t is None else now - self.last_hw_rx_t
        if port_open and self.announced:
            dcl_state = "ok"            # DCL holds the wire + handshake done;
                                        # an idle device is legitimately quiet
                                        # (rx age rides along as detail)
        elif not port_open and not self.announced:
            dcl_state = "bad"           # nothing has ever held the wire
        else:
            dcl_state = "warn"          # half-way: port held pre-handshake,
                                        # or handshake seen but port dropped
        core = ("KickMain", "KickUserInterface", "KickAudioEngine")
        if all(procs[n] for n in core):
            engine_state = "ok"
        elif any(procs.values()):
            engine_state = "warn"
        else:
            engine_state = "bad"
        subs = self.stream_subscribers()
        frame_age = self.streamer.last_frame_age(now)
        if subs == 0:
            stream_state = "off"
        elif frame_age is not None and frame_age <= 2.0:
            stream_state = "ok"
        else:
            stream_state = "warn"
        input_age = None if self.last_input_t is None else now - self.last_input_t
        if input_age is None:
            input_state = "off"
        elif input_age <= 60.0:
            input_state = "ok"
        else:
            input_state = "warn"
        with self._state_lock:
            self._state_seq += 1
            seq = self._state_seq
        return {
            "t": "state",
            "seq": seq,
            "bridge": {"state": "ok", "uptime_s": round(now - self.started_t, 1),
                       "clients": len(self.clients)},
            "dcl": {"state": dcl_state, "announced": self.announced,
                    "port_open": port_open,
                    "last_rx_age_s": None if rx_age is None else round(rx_age, 2)},
            "engine": {"state": engine_state, "processes": procs},
            "stream": {"state": stream_state, "subscribers": subs,
                       "fps": round(self.streamer.fps(now), 2),
                       "last_frame_age_s": None if frame_age is None
                       else round(frame_age, 2),
                       "frames_sent": self.streamer.frames_sent,
                       "burst_frames": self.streamer.burst_frames,
                       "burst_active": now < self.burst_until},
            "input": {"state": input_state, "last_kind": self.last_input_kind,
                      "last_age_s": None if input_age is None
                      else round(input_age, 2)},
        }

    def push_state(self, min_interval=0.0, client=None):
        now = time.time()
        if min_interval and now - self._last_state_push_t < min_interval:
            return
        self._last_state_push_t = now
        snap = self.state_snapshot()
        if client is not None:
            client.send_text(json.dumps(snap))
        else:
            self.broadcast(snap)

    def state_pusher(self):
        """The snapshot heartbeat: every 2 s to all connected clients.
        (VMPC pushes at 10 Hz on change; our facts change slowly and
        the interesting ones — input, announce — also push on event
        via note_input / the state request, so 2 s is honest pacing
        for a health line, not a data channel.)"""
        while True:
            time.sleep(2.0)
            with self.clients_lock:
                any_clients = bool(self.clients)
            if any_clients:
                self.push_state()

    # -- pty ---------------------------------------------------------------
    def open_pty(self):
        self.master_fd, slave_fd = pty.openpty()
        slave_name = os.ttyname(slave_fd)
        # Raw from our side too, so nothing mangles bytes before DCL's
        # own termios config lands (it sets cfmakeraw on open).
        attrs = termios.tcgetattr(slave_fd)
        attrs[0] = 0; attrs[1] = 0; attrs[3] = 0
        attrs[2] = attrs[2] | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(slave_fd, termios.TCSANOW, attrs)
        os.close(slave_fd)
        try:
            if os.path.islink(self.link_path) or os.path.exists(self.link_path):
                os.unlink(self.link_path)
            os.symlink(slave_name, self.link_path)
        except OSError as e:
            log(f"WARNING: cannot link {self.link_path} -> {slave_name}: {e} "
                f"(run as root, or point --link somewhere writable)")
        log(f"pty open: {self.link_path} -> {slave_name} (master fd {self.master_fd})")

    def pty_write(self, frame: bytes, why: str):
        with self.pty_lock:
            os.write(self.master_fd, frame)
        log(f"pty tx [{why}]: {frame.hex()}")

    def send_hw(self, pkt_type, cmd, data, why):
        # The MCU-side transmitter appends a trailing '\n' after the CRC
        # (the app build has it commented out in BuildAndTransmitPacket,
        # and HwSerialData's receiver arithmetic — packetLength = size+7 —
        # only works out with it). Mirror the MCU: frame + '\n'.
        frame = build_frame(pkt_type, DIR_REQUEST, cmd, data) + b"\n"
        self.pty_write(frame, why)

    def pty_reader(self):
        while True:
            try:
                chunk = os.read(self.master_fd, 4096)
            except OSError:
                # EIO is normal here: it is what a pty master returns
                # while no slave end is open (DCL not started yet, or
                # it exited and KickMain is respawning it). Wait and
                # keep reading — the slave comes and goes with DCL.
                time.sleep(0.1)
                continue
            if not chunk:
                time.sleep(0.02)
                continue
            self.last_hw_rx_t = time.time()     # DCL is alive on the wire
            for (t, d, c, data) in self.parser.feed(chunk):
                self.on_hw_frame(t, d, c, data)

    # -- runtime -> web ------------------------------------------------------
    def broadcast(self, obj):
        text = json.dumps(obj)
        with self.clients_lock:
            targets = list(self.clients)
        for cl in targets:
            cl.send_text(text)

    # -- screen streaming ------------------------------------------------------
    def stream_subscribers(self):
        with self.clients_lock:
            return sum(1 for cl in self.clients if cl.wants_stream and cl.alive)

    def send_frame(self, jpeg: bytes):
        with self.clients_lock:
            targets = [cl for cl in self.clients if cl.wants_stream and cl.alive]
        for cl in targets:
            cl.send_binary(jpeg)

    def on_hw_frame(self, t, d, c, data):
        log(f"pty rx: {frame_str(t, d, c, data)}")
        if t == PKT_SYSTEM and c == 2 and not self.announced:
            # DCL sent EN_SYS_COMM_INIT while connecting — it is alive on
            # the wire. Announce ourselves like the MCU would.
            self.announced = True
            self.send_hw(PKT_SYSTEM, 4, [1], "comm-status announce")
        if t == PKT_LED and data:
            code = data[0]
            if c == 0 and len(data) >= 2:                    # SET_SINGLE_LED
                name = LED_CODE_TO_PANEL.get(code)
                if name:
                    self.broadcast({"t": "led", "id": name, "on": data[1] != 0})
                else:
                    log(f"  led code {code} has no panel button — logged only")
            elif c == 1:                                     # BLINK — approx as on
                name = LED_CODE_TO_PANEL.get(code)
                if name:
                    self.broadcast({"t": "led", "id": name, "on": True})
            elif c == 2:                                     # SET_ALL — state in data[1]?
                log("  led set-all: no per-button state decodable — logged only")
        elif t == PKT_PAD and data:
            if c == 1 and len(data) >= 3:                    # SET_SINGLE_PAD
                self.pad_light(data[0], data[1], data[2])
            elif c == 2 and len(data) >= 2:                  # SET_ALL_SINGLE_COLOUR
                for pad in range(16):
                    self.pad_light(pad, data[0], data[1])
            elif c == 5 and len(data) >= 32:                 # COLOUR_STATE bulk
                for pad in range(16):
                    self.pad_light(pad, data[16 + pad], data[pad])
            elif c == 8 and len(data) >= 3:                  # SET_BLINKING
                self.pad_light(data[0], 4, data[2])
            else:
                log(f"  pad cmd {c}: no panel translation — logged only")
        elif t == PKT_SLIDER:
            log("  slider feedback: panel has no strip-LED API — logged only")
        # SYSTEM / MIDI / CV / unknown: logged verbatim above.

    def pad_light(self, pad0: int, state: int, colour_id: int):
        if not (0 <= pad0 < 16):
            return
        self.broadcast({"t": "padlight", "pad": pad0 + 1,
                        "color": PAD_COLOURS.get(colour_id),
                        "level": PAD_STATE_LEVEL.get(state, 1.0)})

    # -- web -> runtime ------------------------------------------------------
    def on_web_message(self, client: WSClient, text: str):
        try:
            msg = json.loads(text)
        except ValueError:
            log(f"ws rx (bad json): {text[:80]}")
            return
        kind = msg.get("t")
        if kind == "state":
            self.push_state(client=client)
            return
        if kind == "stream":
            on = bool(msg.get("on"))
            client.wants_stream = on
            log(f"ws stream {'on' if on else 'off'} from {client.addr} "
                f"(subscribers: {self.stream_subscribers()})")
            reply = {"t": "stream", "on": on}
            if on:
                reply.update({"w": STREAM_W, "h": STREAM_H,
                              "fps": self.streamer.target_fps})
            client.send_text(json.dumps(reply))
            return
        if kind == "hello":
            knobs = msg.get("knobs") or {}
            for k, v in knobs.items():
                if k in self.knob_last:
                    self.knob_last[k] = float(v)
            log(f"ws hello from {client.addr}: knob baselines synced {knobs}")
            return
        if kind == "pad":
            pad0 = int(msg["pad"]) - 1
            if msg.get("down"):
                self.send_hw(PKT_PAD, 0, [pad0, 1, int(msg.get("vel", 127)), 0],
                             f"pad {msg['pad']} down")
            else:
                self.send_hw(PKT_PAD, 0, [pad0, 0, 0, 0], f"pad {msg['pad']} up")
            self.note_input("pad")
            return
        if kind == "button":
            bid = msg.get("id")
            down = bool(msg.get("down"))
            self.on_button(bid, down)
            if down:
                self.note_input("button")
            return
        if kind == "knob":
            kid = msg.get("id")
            if kid not in KNOB_CODES:
                log(f"ws knob {kid}: no code — dropped")
                return
            value = float(msg.get("value", 0.0))
            last = self.knob_last.get(kid, 0.5)
            steps = int(round((value - last) * 127))
            self.knob_last[kid] = value
            if steps:
                direction = 1 if steps > 0 else 0   # 1=CW, 0=CCW (KnobDirection)
                self.send_hw(PKT_KNOBS, 1, [KNOB_CODES[kid], direction, abs(steps)],
                             f"knob {kid} {'+' if steps > 0 else ''}{steps}")
                self.note_input("knob")
            return
        if kind == "jog":
            delta = int(msg.get("delta", 0))
            if delta:
                # The runtime's screens navigate on the ROTARY knob
                # message (KNOBS / EN_KNOB_ROTARY, knob code
                # EN_KNOB_CENTER, direction, steps) — e.g.
                # FileBrowser::handleCommand switches on exactly that
                # and ignores the EN_KNOB_CW/CCW detent BUTTON frames
                # this handler used to send (those are near-no-ops in
                # AppGrid and ignored in the File Browser, which is
                # why the panel's encoder and cursor arrows appeared
                # dead). Direction: 1 = clockwise, 0 = anticlockwise
                # (KnobDirection). One frame, steps = detent count.
                direction = 1 if delta > 0 else 0
                steps = min(abs(delta), 8)
                self.send_hw(PKT_KNOBS, 0, [0, direction, steps],
                             f"jog {'+' if delta > 0 else '-'}{steps} (rotary)")
                self.note_input("jog")
            return
        if kind == "jogpush":
            # Jog push = EN_KNOB_ENTER. Despite the name it is NOT a
            # KNOBS message: in the contracts it is a BtnLedSequence
            # code (dcl_contracts.h: EN_KNOB_ENTER = 26) carried by a
            # BUTTON / EN_BTN_STATUS frame, exactly like a button
            # press. DclContractsHelper::isCenterKnobClickMessage
            # (dcl_contracts_helper.cpp) is defined that way, and the
            # consumers switch on it that way — e.g.
            # FileBrowser::handleCommand (kick_fileBrowser.cpp):
            # isButtonMessage && code == EN_KNOB_ENTER && state ==
            # BTN_PRESSED -> knobClicked() (open/confirm). The
            # upstream sim emits the same code from its encoder push
            # (kick_SimulatorMainControl.cpp). Press AND release are
            # forwarded, mirroring every other panel button; screens
            # act on BTN_PRESSED.
            down = bool(msg.get("down"))
            self.send_hw(PKT_BUTTON, 0, [EN_KNOB_ENTER, 1 if down else 0],
                         f"jog push {'down' if down else 'up'} (EN_KNOB_ENTER)")
            if down:
                self.note_input("jogpush")
            return
        if kind == "strip":
            pct = int(round(float(msg.get("value", 0.0)) * 100))
            self.send_hw(PKT_SLIDER, 0, [max(0, min(100, pct))], f"strip {pct}%")
            self.note_input("strip")
            return
        log(f"ws rx (unhandled): {text[:120]}")

    def on_button(self, bid, down: bool):
        if bid == "INPUT":
            log("ws button INPUT: no runtime counterpart — not sent")
            return
        if bid in ("CUR_UP", "CUR_DOWN", "CUR_LEFT", "CUR_RIGHT"):
            # Retired translations (EN_PLUS/EN_MINUS approximations and
            # EN_KNOB_CW/CCW detent frames): the runtime's screens do
            # not consume either shape for navigation — the File
            # Browser answers only the ROTARY knob message. The page
            # now emits a 'jog' event for cursor-ring presses (see
            # template.html buttonDown), which the jog handler above
            # sends in the rotary shape. These button frames are
            # dropped so a press steps the encoder exactly once.
            if down:
                log(f"ws button {bid}: superseded by jog (rotary) path — not sent")
            return
        pair = BUTTON_CODES.get(bid)
        if pair is None:
            log(f"ws button {bid}: no runtime code — not sent")
            return
        if bid == "SHIFT":
            self.shift_held = down
            self.send_hw(PKT_BUTTON, 0, [EN_SHIFT, 1 if down else 0], "SHIFT")
            return
        if down:
            code = pair[1] if self.shift_held else pair[0]
            self.emitted_code[bid] = code
            self.send_hw(PKT_BUTTON, 0, [code, 1], f"button {bid} down (code {code})")
        else:
            code = self.emitted_code.pop(bid, pair[0])
            self.send_hw(PKT_BUTTON, 0, [code, 0], f"button {bid} up (code {code})")

    # -- servers ---------------------------------------------------------------
    def ws_server(self, host: str, port: int):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(8)
        log(f"websocket listening on ws://{host}:{port}")
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=self.handle_ws_client, args=(conn, addr),
                             daemon=True).start()

    def handle_ws_client(self, conn: socket.socket, addr):
        try:
            req = b""
            while b"\r\n\r\n" not in req:
                chunk = conn.recv(4096)
                if not chunk:
                    return
                req += chunk
            key = None
            for line in req.decode("latin1").split("\r\n"):
                if line.lower().startswith("sec-websocket-key:"):
                    key = line.split(":", 1)[1].strip()
            if not key:
                conn.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
                return
            accept = base64.b64encode(
                hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
            conn.sendall(("HTTP/1.1 101 Switching Protocols\r\n"
                          "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                          f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
        except OSError:
            return
        client = WSClient(conn, addr)
        with self.clients_lock:
            self.clients.append(client)
        log(f"ws client connected: {addr}")
        client.send_text(json.dumps({"t": "ready"}))
        self.push_state(client=client)      # health dots light up at once
        try:
            for text in client.frames():
                self.on_web_message(client, text)
        except (ConnectionError, OSError):
            pass
        finally:
            client.alive = False
            with self.clients_lock:
                if client in self.clients:
                    self.clients.remove(client)
            log(f"ws client disconnected: {addr}")
            try:
                conn.close()
            except OSError:
                pass


class PanelHandler(SimpleHTTPRequestHandler):
    """Serves the panel directory, plus GET /state.json — the same
    health snapshot the WS pushes as {"t":"state"} (fourth addendum),
    for curl-level debugging and HTTP fallbacks."""

    bridge = None       # set in main()

    def do_GET(self):
        if self.path.split("?", 1)[0] == "/state.json" and self.bridge is not None:
            body = json.dumps(self.bridge.state_snapshot()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1",
                    help="bind address for both servers (default 127.0.0.1; "
                         "0.0.0.0 exposes the panel on all interfaces)")
    ap.add_argument("--ws-port", type=int, default=8794)
    ap.add_argument("--http-port", type=int, default=8793)
    ap.add_argument("--no-http", action="store_true")
    ap.add_argument("--link", default="/dev/ttymxc0")
    ap.add_argument("--stream-fps", type=int, default=5,
                    help="screen-stream target frame rate (default 5)")
    ap.add_argument("--stream-quality", type=int, default=60,
                    help="screen-stream JPEG quality (default 60)")
    ap.add_argument("--display", default=":99",
                    help="X display holding the Kick GUI window (default :99)")
    args = ap.parse_args()

    bridge = Bridge(args.link, stream_fps=args.stream_fps,
                    stream_quality=args.stream_quality,
                    stream_display=args.display)
    bridge.open_pty()
    threading.Thread(target=bridge.pty_reader, daemon=True).start()
    threading.Thread(target=bridge.streamer.run, daemon=True).start()
    threading.Thread(target=bridge.state_pusher, daemon=True).start()

    if not args.no_http:
        panel_dir = os.path.dirname(os.path.abspath(__file__))
        PanelHandler.bridge = bridge
        handler = lambda *a, **kw: PanelHandler(*a, directory=panel_dir, **kw)
        httpd = ThreadingHTTPServer((args.host, args.http_port), handler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        log(f"panel served at http://{args.host}:{args.http_port}/index.html "
            f"(health snapshot at /state.json)")

    try:
        bridge.ws_server(args.host, args.ws_port)
    except KeyboardInterrupt:
        log("bye")


if __name__ == "__main__":
    main()
