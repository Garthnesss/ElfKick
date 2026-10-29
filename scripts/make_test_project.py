#!/usr/bin/env python3
"""ElfKick content generator: first content-bearing Kick project.

Writes /home/Kick/projects/ElfTest01/ containing:
  ElfTest01.kickPrj  -- JUCE ValueTree XML per PROJECT_FORMAT.md §6
  ElfTest01.png      -- home-screen tile (copied from a shipped demo tile)
  kick.wav           -- synthesised pitch-swept sine kick (stdlib only)

The project carries one Drum program ("Elf Drums", type 0) whose pads
(bank A, MIDI notes 0-15 -- the pad-note space is 0-based, see
FIRST_PAD_NOTE below) all trigger kick.wav through AudIO DrumVoice
sample playback, plus one sequence/track linked to the program by its
explicitly authored id ("ElfProg1" -- an empty id would be randomised
on load and break the track link).

Key format invariants (PROJECT_FORMAT.md §3):
  * The FILE asset `path` string is used verbatim as the file location
    AND as the slice-lookup key, so every LAYER `source` must be
    character-identical to it.
  * No checksum / version gate on load; omitted attributes default.

Idempotent: re-running rewrites the same files. Prints what it wrote.
"""

import math
import shutil
import struct
import sys
import wave
from pathlib import Path

PROJECT_NAME = "ElfTest01"
PROJECTS_ROOT = Path("/home/Kick/projects")
PROJECT_DIR = PROJECTS_ROOT / PROJECT_NAME
WAV_NAME = "kick.wav"
WAV_PATH = str(PROJECT_DIR / WAV_NAME)  # must match LAYER source verbatim
PROGRAM_ID = "ElfProg1"
FILEMANAGER_ID = "ElfFM001"
FILE_ID = "ElfFile1"
FIRST_PAD_NOTE = 0  # pad-note space is 0-based: bank A pads send notes
NUM_PADS = 16       # 0-15 (AppMainmode::handleCommand adds a 16-per-bank
                    # offset via getBankOffset(); the track/program path
                    # applies no transposition -- verified in source and by
                    # the PAD_GATE_LOG trace: Pad 0 delivers noteOn(0))

# Repo root = parent of this script's directory (scripts/..), used only to
# find a tile png to copy.
REPO_ROOT = Path(__file__).resolve().parent.parent
TILE_CANDIDATES = [
    REPO_ROOT / "Kick" / "demos" / "Demo 01" / "Demo 01.png",
    Path("/app/assets/Default/recent_project/Demo 01.png"),
    Path("/app/demos/Demo 1/Demo 01.png"),
]


def synth_kick_wav(path: Path, sample_rate: int = 48000,
                   duration: float = 0.4) -> None:
    """Pitch-swept sine burst: 150 Hz -> 45 Hz exponential sweep with an
    exponential amplitude decay. 16-bit mono PCM."""
    n = int(sample_rate * duration)
    frames = bytearray()
    phase = 0.0
    for i in range(n):
        t = i / sample_rate
        frac = i / n
        freq = 45.0 + (150.0 - 45.0) * math.exp(-frac * 6.0)
        phase += 2.0 * math.pi * freq / sample_rate
        amp = math.exp(-t * 12.0)  # ~0.4 s audible decay
        sample = int(0.9 * 32767 * amp * math.sin(phase))
        frames += struct.pack("<h", sample)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(bytes(frames))


def build_kickprj() -> str:
    clips = []
    for note in range(FIRST_PAD_NOTE, FIRST_PAD_NOTE + NUM_PADS):
        clips.append(
            f'        <CLIP keyNote="{note}" minNote="{note}" maxNote="{note}">\n'
            f"          <LAYERS>\n"
            f'            <LAYER source="{WAV_PATH}" slice="0"/>\n'
            f"          </LAYERS>\n"
            f"        </CLIP>"
        )
    clips_xml = "\n".join(clips)
    return f"""<?xml version="1.0" encoding="UTF-8"?>

<ROOT name="{PROJECT_NAME}" manufacturer="Musictribe" version="0.1" bpm="120.0" length="4" num="4" den="4" isMasterBPM="1">
  <BUSES/>
  <FILEMANAGER id="{FILEMANAGER_ID}">
    <ASSETS>
      <FILE id="{FILE_ID}" path="{WAV_PATH}"/>
    </ASSETS>
    <AUDIO/>
    <EXPORTS/>
  </FILEMANAGER>
  <PROGRAMS>
    <PROGRAM name="Elf Drums" type="0" id="{PROGRAM_ID}" OUT="Out-1,2">
      <CLIPS>
{clips_xml}
      </CLIPS>
    </PROGRAM>
  </PROGRAMS>
  <SEQUENCES>
    <SEQUENCE name="Sequence 01" length="4">
      <TRACK name="Drums" program="{PROGRAM_ID}"/>
    </SEQUENCE>
  </SEQUENCES>
</ROOT>
"""


def main() -> int:
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    written = []

    prj_path = PROJECT_DIR / f"{PROJECT_NAME}.kickPrj"
    prj_path.write_text(build_kickprj(), encoding="utf-8")
    written.append(prj_path)

    wav_path = PROJECT_DIR / WAV_NAME
    synth_kick_wav(wav_path)
    written.append(wav_path)

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
