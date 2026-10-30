#!/usr/bin/env python3
"""Throttled FIFO reader for rig audio capture (see asound-fifo.conf).
Usage: rig_capture_reader.py [bytes_per_sec] [out_path]"""
import os, sys, time

rate = int(sys.argv[1]) if len(sys.argv) > 1 else 768000
out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser(
    "~/workspace/elfkick/rig-capture.raw")
fifo = "/tmp/rig.fifo"
if not os.path.exists(fifo):
    os.mkfifo(fifo)
written = 0
start = time.time()
with open(fifo, "rb", buffering=0) as f, open(out_path, "wb") as out:
    while True:
        chunk = f.read(16384)
        if not chunk:
            break
        out.write(chunk)
        written += len(chunk)
        # pace: sleep until wall-clock matches the byte budget
        target = start + written / rate
        delay = target - time.time()
        if delay > 0:
            time.sleep(delay)
print(f"reader done: {written} bytes")
