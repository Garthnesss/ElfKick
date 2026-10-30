#!/usr/bin/env python3
"""ElfKick content generator: ElfSynth01 — the first synth project.

Writes /home/Kick/projects/ElfSynth01/ containing:
  ElfSynth01.kickPrj       -- one Synth program (type 8, "Elf Synth",
                              id "ElfSynth1") carrying the SYNTH subtree
                              of SYNTH_DESIGN.md §6.1 verbatim, program
                              master semi="36"
  ElfSynth01.png           -- home-screen tile (copied from a demo tile)
  Sequence 01/Sequence 01.mid -- demo pattern: 4 bars of triads
                              C - F - G - Am starting at note 60

Sounding pitches (semi=36 transposes +3 octaves via the program-master
pitch ratio): pad bank A (notes 0-15) sounds at 65.4-155.6 Hz; the demo
chords (notes 60-76) sound at ~2.1-3.1 kHz. Retune by editing the XML —
that is what the format is for.

Idempotent: re-running rewrites the same files. Prints what it wrote.
Stdlib only.
"""

import shutil
import struct
import sys
from pathlib import Path

PROJECT_NAME = "ElfSynth01"
PROJECTS_ROOT = Path("/home/Kick/projects")
PROJECT_DIR = PROJECTS_ROOT / PROJECT_NAME
PROGRAM_ID = "ElfSynth1"
FILEMANAGER_ID = "ElfFM001"
SEQ_NAME = "Sequence 01"

REPO_ROOT = Path(__file__).resolve().parent.parent
TILE_CANDIDATES = [
    REPO_ROOT / "Kick" / "demos" / "Demo 01" / "Demo 01.png",
    Path("/app/assets/Default/recent_project/Demo 01.png"),
    Path("/app/demos/Demo 1/Demo 01.png"),
]

KICKPRJ = f"""<?xml version="1.0" encoding="UTF-8"?>

<ROOT name="{PROJECT_NAME}" manufacturer="Musictribe" version="0.1" bpm="120.0" length="4" num="4" den="4" isMasterBPM="1">
  <BUSES/>
  <FILEMANAGER id="{FILEMANAGER_ID}">
    <ASSETS/>
    <AUDIO/>
    <EXPORTS/>
  </FILEMANAGER>
  <PROGRAMS>
    <PROGRAM name="Elf Synth" type="8" id="{PROGRAM_ID}" OUT="Out-1,2" semi="36">
      <SYNTH glide="0.00" spread="0">
        <OSC index="0" isEnabled="1" wave="0" semi="0" fine="0" level="110" width="0"/>
        <OSC index="1" isEnabled="1" wave="0" semi="0" fine="7" level="90" width="0"/>
        <OSC index="2" isEnabled="1" wave="1" semi="-12" fine="0" level="70" width="64"/>
        <ENVELOPE>
          <AMPENV type="0" isEnabled="1" attack="0.005" hold="0" decay="0.25" sustain="0.8" release="0.30"/>
          <FILTERENV type="0" isEnabled="1" attack="0.010" hold="0" decay="0.40" sustain="0.4" release="0.30" freq="8500" resonance="0.7" depth="48"/>
          <PITCHENV type="0" isEnabled="0" attack="0.005" hold="0" decay="0.20" sustain="0" release="0.20" depth="0"/>
        </ENVELOPE>
        <LFO isEnabled="0" type="0" freq="5.0" pitch="0" filter="0" level="0" pan="0"/>
      </SYNTH>
    </PROGRAM>
  </PROGRAMS>
  <SEQUENCES>
    <SEQUENCE name="{SEQ_NAME}" length="4">
      <TRACK name="Synth" program="{PROGRAM_ID}"/>
    </SEQUENCE>
  </SEQUENCES>
</ROOT>
"""

# Demo progression: (bar, [notes]) — C, F, G, Am triads.
CHORDS = [
    (0, [60, 64, 67]),  # C
    (1, [65, 69, 72]),  # F
    (2, [67, 71, 74]),  # G
    (3, [69, 72, 76]),  # Am
]
PPQ = 480
BAR_TICKS = PPQ * 4
VELOCITY = 100


def vlq(n: int) -> bytes:
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def build_mid() -> bytes:
    events = []  # (tick, bytes-without-delta)
    events.append((0, b"\xff\x03" + bytes([len(SEQ_NAME)]) + SEQ_NAME.encode()))
    events.append((0, b"\xff\x51\x03" + struct.pack(">I", 500000)[1:]))  # 120 BPM
    events.append((0, b"\xff\x58\x04\x04\x02\x18\x08"))  # 4/4
    for bar, notes in CHORDS:
        start = bar * BAR_TICKS
        end = start + BAR_TICKS - 10
        for note in notes:
            events.append((start, bytes([0x90, note, VELOCITY])))
            events.append((end, bytes([0x80, note, 0])))
    events.sort(key=lambda e: e[0])
    track = bytearray()
    last = 0
    for tick, data in events:
        track += vlq(tick - last)
        track += data
        last = tick
    track += b"\x00\xff\x2f\x00"  # end of track
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
    return header + b"MTrk" + struct.pack(">I", len(track)) + bytes(track)


def main() -> int:
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    written = []

    prj_path = PROJECT_DIR / f"{PROJECT_NAME}.kickPrj"
    prj_path.write_text(KICKPRJ, encoding="utf-8")
    written.append(prj_path)

    seq_dir = PROJECT_DIR / SEQ_NAME
    seq_dir.mkdir(parents=True, exist_ok=True)
    mid_path = seq_dir / f"{SEQ_NAME}.mid"
    mid_path.write_bytes(build_mid())
    written.append(mid_path)

    tile_dest = PROJECT_DIR / f"{PROJECT_NAME}.png"
    tile_src = next((c for c in TILE_CANDIDATES if c.is_file()), None)
    if tile_src is not None:
        shutil.copyfile(tile_src, tile_dest)
        written.append(tile_dest)
    else:
        print("warning: no demo tile png found; tile skipped "
              "(project still loads; home screen shows a text tile)")

    for p in written:
        print(f"wrote {p} ({p.stat().st_size} bytes)")
    print(f"project dir: {PROJECT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
