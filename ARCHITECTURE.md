# ElfKick — Architecture

How the KICK firmware is put together, as released upstream
(MusicTribeOpenSource/0700-AAM @ 2719418) and verified by building it.
Claims marked *(inferred)* are read from config/code, not from running hardware.

## Big picture

```
┌──────────────────────── i.MX8M Mini (aarch64, Buildroot Linux) ───────────────────────┐
│  kick_main (supervisor, heartbeat)                                                    │
│     ├── kick_gui            JUCE touchscreen UI                                       │
│     ├── kick_audio_engine   owns AudIO designs, answers IPC allocate/connect/…        │
│     ├── kick_audio_model    project/song model                                        │
│     └── kick_device_control hardware events ⇄ PktMgr                                  │
│            │  IPC: in-tree mt_ipc (Unix sockets, JSON messages, process restarter)     │
│            │  USB (FunctionFS / serial 230400 baud via juce_serialport)                │
└────────────┼──────────────────────────────────────────────────────────────────────────┘
             │  PktMgr framed packets (header, type, dir, id, size, payload, CRC)
┌────────────▼──────────────── hardware MCU (firmware NOT released) ────────────────────┐
│  pads + RGB LEDs · buttons · knobs · touch strip/slider · MIDI · CV/Gate              │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

Two computers, like most modern grooveboxes: a Linux application processor
does UI + audio; a dumb MCU owns the controls and is spoken to in tiny framed
packets — `ComProtocol/PktMgr`, packet types SYSTEM, BUTTON, LED, PAD,
KNOBS, MIDI, CV, SLIDER, 30-byte Rx FIFO, CRC byte.

## Hardware (from the Linux tree)

- SoC: Variscite SOM on **NXP i.MX8M Mini** (`BR2_PACKAGE_FREESCALE_IMX_PLATFORM_IMX8MM=y`),
  aarch64, board files `Linux/buildroot/board/freescale/imx8mm_var_som/` *(upstream tree, not vendored here)*.
- Kernel/U-Boot: varigit forks (`linux-imx`, `uboot-imx`, pinned commits in
  `KTKick_defconfig`).
- Display stack: Weston + Xorg both present in the defconfig; the app is a
  JUCE program (JUCE 7.0.12, multitouch patch in `Kick/patches/`).
- Audio: ALSA + JACK2 in the image; on the app side, AudIO (below).
- The released Linux image is a **development build**: root has no password
  and Dropbear SSH is enabled (upstream's own warning).

## The application (Kick/)

- **JUCE module architecture.** ~20 in-tree modules (`common`, `hardware`,
  `ipc`, `contracts`, `engine`, `kick_ui*`, …) plus the audio engine as an
  external JUCE module. Apps are separate processes (below), each a thin
  `juce_add_gui_app`/console target linking the module set.
- **Processes** (`Kick/apps/`): `kick` (combined), `kick_main` (supervisor +
  heartbeat monitor + log), `kick_gui`, `kick_audio_engine`,
  `kick_audio_model`, `kick_device_control`, plus utilities
  (`kick_device_control_test`, `midiPort`, `multitouch` demo), the
  **`kickem` simulator** (desktop hardware simulator — see STATUS.md), and
  `kickvst` — which is only the stock JUCE plugin template, a placeholder.
- **IPC** (`modules/ipc`): socket connections, JSON messages, a dispatcher,
  handler store, child-process manager and restarter. The audio engine app
  exposes message handlers: allocate / deallocate / connect / disconnect /
  set/get property(ies) / get object status.
- **Engine** (`modules/engine`): samplers (`ClipSampler`, `DrumSampler`,
  `KeygroupSampler` over a common `ISampler`, smb + stft pitch shifters),
  audio file manager, grouping, MIDI, program/synth, project model.
- **Hardware module** (`modules/hardware`): I2C, USB FunctionFS gadget,
  firmware update, WiFi firmware, preferences/resources.
- **input_events** talks to the MCU: serial port at **230400 baud**
  (`juce_serialport`, removed from the release) and PktMgr framing.

## The audio engine (AudIO/)

- A **patchable graph** library: `Design` (a graph) contains objects,
  chains and buses; objects are created by name from a self-registering
  `ObjectFactory` (`REGISTER_OBJECT` in each object header).
- Core: `Design/DesignImpl` (topological sort, cycle detection, meter
  engine), `Chain`, `Bus`, SPSC queues, a fixed **memory pool** of audio
  buffers (examples use 20–30 blocks of 960 samples @ 48 kHz).
- Objects: SineWave (+ADSR variant), ADSR, Mixer, VolumeAndPan, LevelMeter,
  PlayBack, Recorder, AudioReaderWriter (AudioFile-based sampler), SFSynth.
- **DAW layer** (`AudIO/Daw`): DawEngine (a Design subclass), buses/routing,
  tracks, sequences, inserts — sequences/tracks are behind
  `AUDIO_DAW_EXTENSIONS`, **0 in the shipped config**.
- **Effects**: 329 Airwindows plugin sources, wrapped as one static library
  (`AudIO_airwindows`), MIT-licensed upstream code by Chris Johnson.
- **SoundFont synth**: TinySoundFont (`tsf.h`) behind `SFSynth`. The embedded
  GM font was removed by upstream (E-mu copyright) — `AWE32_1MB.h` is a
  0-length stub, so SFSynth starts font-less.
- **Build model — the important part.** AudIO is a **JUCE unity-build
  module**: `AudIO.cpp` `#include`s every .cpp in a curated order with
  feature guards (`AudIO.h` config: DAW core on; DAW extensions, SFSynth,
  PlayBack, Recorder, AudioReaderWriter all **off**). The standalone
  `AudIO/CMakeLists.txt` (per-TU glob build) is a second, unmaintained path —
  most of BUILD_FINDINGS.md follows from that.
- **Devices**: `StereoOut`/`StereoInOut` over a vendored **minisdl**
  (SDL 1.2-style reimplementation, ALSA backend). Both device classes are
  commented out of the unity build; the Kick app uses JUCE audio devices.
  Only the standalone examples use minisdl devices.

## What's missing from the release (architecture view)

Pieces the code references but the release does not contain — each had to be
reconstructed or stubbed to configure the app build (STATUS.md has state):

- The **private AudIO git repo** (CPM fetches it from a placeholder URL) —
  the public `AudIO/` folder is its export, minus `juce_modules.cmake`.
- **`AudIO_constants`** and **`AudIO_juce`** modules (private AudIO repo
  siblings). The public `AudIO.h` JUCE-extensions block is empty.
- **`juce_serialport`** (removed, "no license") — cpr2323's module matches
  the used API; it has no Linux implementation.
- Content: factory samples, the UI font, the GM SoundFont, the prebuilt
  aarch64 SDK tarball.
- The **MCU firmware** itself — only the application-side protocol
  (PktMgr) is released.
- **Camera/gesture**: advertised as a headline feature; zero camera,
  gesture or vision code exists anywhere in the released tree.
