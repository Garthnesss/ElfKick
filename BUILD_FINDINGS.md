# Behringer KICK (0700-AAM) — build attempt findings

Date: Oct 28, 2026 · VM: Ubuntu 24.04, gcc 13.3, CMake 3.28, 2 cores
Source: `0700-AAM/` @ 2719418 (pristine clone, untouched)
Patched working copies: `work/AudIO-fixed/`, `work/ComProtocol-fixed/`
Patch file: `audio-linux-fixes.patch` (21 files, all changes listed below)
Build dirs: `build-audio/` (Release), `build-audio-asan/` (Debug+ASan), `build-kick-pristine/`, `build-kick/`

## TL;DR

- **AudIO standalone: BUILDS and RUNS after patches.** All 3 libraries
  (AudIO_Core, DAW_Core, AirWindows_plugins) and all 4 console examples
  (BasicExample, DynamicPatch, Subdesign, DawExample) compile and link.
  DynamicPatch and DawExample run indefinitely (killed by timeout);
  BasicExample and Subdesign build but crash at runtime (causes below).
- **Kick simulator (kickem): pristine configure FAILS** — CPM tries to clone
  AudIO from the placeholder `https://example.com/your-git-server/AudIO`.
  The private AudIO/ComProtocol repos are not part of the release, and the
  glue files the build expects from them (`juce_modules.cmake`, a JUCE module
  declaration for PktMgr) are not in the public tree either. Workaround with
  local-source overrides + reconstructed glue: see end of this file.

## Why the standalone AudIO build was never exercised upstream

AudIO is designed as a **JUCE unity-build module**: `AudIO/AudIO.cpp`
`#include`s every .cpp in a curated order (Design.cpp before DesignImpl.cpp,
feature-guarded objects). The standalone `AudIO/CMakeLists.txt` instead globs
each .cpp as its own translation unit — and that path is broken in ways that
cannot compile on *any* platform, plus Windows-isms that only break on Linux.
The shipped feature config (`AudIO/AudIO.h`) also explains the bit-rot:
`AUDIO_DAW_EXTENSIONS=0`, `AUDIO_OBJECT_SF_SYNTH=0`,
`AUDIO_OBJECT_AUDIO_FILE_PLAYBACK=0`, `AUDIO_OBJECT_AUDIO_FILE_RECORDER=0`,
`AUDIO_OBJECT_AUDIO_READER_WRITER=0` — the optional objects are excluded from
the real build, so their drift was invisible.

## Patches applied to work/AudIO-fixed (in discovery order)

Case-sensitivity (Windows dev, case-insensitive FS):
1. `CMakeLists.txt`: glob/include `AudIO/core` → `AudIO/Core` (dir is `Core/`).
2. Example CMakeLists (BasicUsage, Subdesign, DynamicPatching, Synth):
   `main.cpp` → `Main.cpp`. (Pristine configure fails here, CMake generate step.)

Unity-build assumptions (fail on any compiler when compiled per-TU):
3. `Core/DesignImpl.h`: now `#include "../Includes/Design.h"` (it uses
   `std::shared_ptr<Design>` but never included it; DesignImpl.cpp standalone
   failed with "'Design' was not declared").
4. `Core/DesignImpl.h`: `_find()` free function in a header → `inline`
   (was a multiple-definition link error across TUs).
5. `Core/DesignImpl.cpp`: removed stray `inline` from out-of-line definitions
   of `DesignImpl::process` and `DesignImpl::clear` (called cross-TU from
   Design.cpp → undefined references).
6. `Dsp/lfo.h`: removed `inline` from `advance_internal` /
   `nextSample_internal` declarations (definitions live in lfo.cpp; the inline
   declaration suppressed the out-of-line symbol → undefined reference from
   SineWave).

Missing standard includes (headers not self-contained):
7. `Includes/AudioDataTypes.h`, `3rdparty/AudioFile.h`: `<cstdint>`.
8. `Objects/ADSR.h`, `Objects/PlayBack.h`, `Objects/Recorder.h`,
   `Daw/DawInserts.h`: `<atomic>`.
9. `Daw/DawRouting.cpp`: `<thread>` + `<chrono>`; `Daw/DawEngine.cpp`: `<thread>`;
   `Example/Daw/DawExampleMain.cpp`: `<thread>` + `<chrono>`.

Dead/stale code vs the current Object API:
10. `Devices/StereoOut.cpp`, `Devices/StereoInOut.cpp`: called nonexistent
    `doProcess()` → `IAudioDevice::process(_buffer)` (base-class callback
    dispatch; derived `process(float*)` hides it, hence the qualification).
    Note both device files are *excluded* from the unity build
    (StereoOut.cpp is commented out in AudIO.cpp) — the examples are their
    only consumers.
11. `Objects/SFSynth.h/.cpp`: parameter API drift — base `Object` uses
    `setParameter(int, float)` etc.; SFSynth still declared `(uint32_t, …)`
    with `override` (hard error). Changed to `int`. Also used private
    `Object::_sampleRate/_blockSize` → `Object::prepareToPlay()` +
    `blockSize()` accessor, matching every other object.
12. `Objects/SFSynth.cpp` `prepareToPlay()`: added the `_tsf` null guard every
    other SFSynth method already has (runtime fix, see below).

Build configuration:
13. `CMakeLists.txt`: `target_compile_definitions(DAW_Core PUBLIC
    AUDIO_DAW_EXTENSIONS=1)` — DawExample uses `createSequence/playSequence`,
    which are `#if AUDIO_DAW_EXTENSIONS` in DawEngine.h; the standalone build
    never defined it. (The JUCE config sets it to 0; enabling it in the
    standalone DAW lib compiles clean.)

## Runtime results (Release binaries, ALSA null PCM via ALSA_CONFIG_PATH)

The VM has no sound card. The vendored minisdl (SDL 1.2 reimplementation in
`Devices/minisdl_audio.c`) ignores `SDL_AUDIODRIVER=dummy`, opens ALSA
`default`, and its audio thread null-derefs in `ALSA_GetDeviceBuf` when no
card exists. Workaround: an `asound.conf` with `pcm.!default { type null }`.

- **DynamicPatch — RUNS.** Continuous meter output for Bus LL/RR/L+R and
  Master OUT (all −100 dB: graph is silent, but the engine, device thread and
  meter pipeline are alive). Survived until timeout (exit 124).
- **DawExample — RUNS.** Survived until timeout (exit 124, output buffered).
- **BasicExample — starts, then aborts:** prints `USED MEM: 18` and the
  Console help, starts the device, then `terminate called without an active
  exception`. The only bare `throw;` in the entire codebase is
  `SFSynth::setParameter`'s default case (SFSynth.cpp:60) — something calls it
  with an out-of-enum index during/after device start. (First crash, fixed by
  patch 12: `SFSynth::prepareToPlay` dereferenced `_tsf == nullptr` —
  ASan trace: tsf.h:2266 ← SFSynth.cpp:128 ← Chain::createInsert ←
  Design::createInsert ← BasicUsage/Main.cpp:67. Root cause: the constructor
  loads the embedded GM SoundFont, which in this release is the stub
  `AWE32_1MB.h` with `soundfontsize = 0`, so load fails and `_tsf` stays null.)
- **Subdesign — crashes in the example itself:** ASan trace: SEGV at
  `Example/Subdesign/Main.cpp:56`, `pSine2->setName(...)` where
  `pMainDesign->getObject(sine2)` returned nullptr for a just-created
  SineWave in the main design (objects in the two sub-designs resolved fine).
  Looks like a real Design/DesignImpl object-registry bug around
  insertDesign/id allocation; not triaged further.

## Kick simulator (kickem) — pristine attempt

`cmake -S Kick -B build-kick-pristine -DKICK_SIMULATOR=ON`:
- JUCE 7.0.12 clones fine via CPM, multitouch patch found, juceaide builds.
  (Non-fatal: libcurl and webkit2gtk-4.0 pkg-config modules missing on VM.)
- Then 3rdParty.cmake tries to clone AudIO from
  `https://example.com/your-git-server/AudIO` — `fatal: repository not found`
  ×3, configure aborts at 3rdParty.cmake:84. ComProtocol would fail the same
  way. This is the "private dependencies are not included" caveat from
  Kick/README, made concrete.

Workaround attempt (local copies + reconstructed glue):
- `-DCPM_AudIO_SOURCE=<work/AudIO-fixed>` + authored `juce_modules.cmake`
  (`juce_add_modules(.../AudIO)` — the file the private repo shipped).
  AudIO then populates locally and the module registers clean.
- `-DCPM_COM_PROTOCOL_SOURCE=<work/ComProtocol-fixed>` + authored
  `PktMgr/PktMgr.h` JUCE module declaration wrapper around PktMgr.hpp
  (juce_add_modules needs a declaration header; the public PktMgr folder has
  none). PktMgr.cpp then compiles as a module TU in the app build.
- Next missing piece: `Kick/modules/3rdParty/juce_serialport` was removed
  entirely (README: "no license") but is still referenced by
  modules/3rdParty/CMakeLists and used by input_events (230400 baud link to
  the hardware MCU). Identified as cpr2323/juce_serialport (API match:
  SerialPortConfig / SerialPortInputStream / 3-arg SerialPort ctor) and
  cloned in. Note it has **no Linux implementation** (Win/OSX/Android/iOS
  only) and its current header is not self-contained (patched locally with
  <functional> + juce_core/juce_events includes).
- Next: `common` module needs `AudIO_constants` — another private AudIO-repo
  module absent from the release. Stubbed (empty `AudIO::Constants`
  namespace, JUCE declaration) in AudIO-fixed; no symbols were demanded from
  it by the modules that compiled.
- Next: `audio_interface` module is an empty shell whose only job is
  `#include <AudIO_juce/AudIO_juce.h>` — a third missing private module.
  Stubbed likewise (the public AudIO.h `AUDIO_JUCE_EXTENSIONS` block is
  empty, so a stub is faithful to the released configuration).
- Next: kickem's own CMakeLists is the only app without
  `configure_file(config.h.in …)` — every module includes the generated
  `config.h`. Added it (in Kick-fixed).
- With all of that, **the full Kick configure SUCCEEDS** (all apps incl.
  kickem generate; non-fatal noise: libcurl/webkit2gtk/libserial pkg-config
  probes missing on the VM).

Simulator build (`KT_KickEM`): reaches ~50% — JUCE modules, PktMgr, common
and most UI modules compile — then fails in the simulator's own code:
~1,900 error lines rooted in `apps/kickem/simulator/kick_Simulator.cpp`,
which references `mt::Kick::PadAction`, `mt::Kick::PadButtonState`,
`mt::Kick::HwControlCommands`, `mt::Kick::getPadColourFromId`,
`TOTAL_DRUM_PADS`, `Displays`, `AppHome` — none of which exist in the
shipped modules — plus a sigslot signature mismatch in input_events.
**Verdict: as released, kickem was written against a different internal
module API than the one published; it is not a few missing files away from
building.** Stopped there (Oct 28) to move the work into the ElfKick repo,
where fixing the simulator against the published modules is the open task.

## Update — simulator reconciliation (Oct 28, 2026, ElfKick tree)

The open task above is done, bar the final link + launch check. Full
itemised log: `SIM_BUILD_LOG.md`. Headlines:

- The earlier verdict needs one correction: the "different internal API"
  turned out to be mostly the *current* published API under older names —
  the contracts module (`contracts::DeviceControlContracts`,
  `BtnLedSequence`, `PadSequence`, `BtnState`) covers nearly everything
  kickem referenced. The migration was mechanical once mapped.
- Two pieces genuinely never shipped and had to be reconstructed:
  `AudIO_constants` (load-bearing — global MIDI/GAIN/PAN/POLYPHONY
  constants + `UNKNOWN_VALUE` + audio geometry, inferred from call
  sites) and `AudIO_juce` (`mt::SampleRecorder` / `mt::Looper` /
  `mt::AudioDevice` / `mt::ADSR` / `mt::Filter`, adapted over the
  published `DRecorder` / `AudioRecorder`). Both are marked
  RECONSTRUCTED in-tree.
- Enabling the recorder classes required flipping two AudIO feature
  defaults (`AUDIO_DAW_EXTENSIONS`, `AUDIO_OBJECT_AUDIO_FILE_RECORDER`)
  — the switches in `AudIO.h` are now `#ifndef`-guarded so either build
  can override them.
- `juce_serialport` has no Linux implementation upstream at all; the
  simulator path now uses a repo-kept fail-safe stub backend.
- Build trajectory: ~1,960 error lines → 361 → 26 → 1 → **all
  translation units compile**; the last failures were link-only
  (kickem never linked the `contracts` module; `juce_dsp` also needed).

## Cross-check vs sibling release 0716-AAF "IGNITE" (Oct 28, 2026)

MusicTribeOpenSource's only other product repo, 0716-AAF (a 64-channel
sequencer, pushed the same week), ships the **same AudIO tree**:
`diff -rq` over the whole tree = 14 differing files, and every one
differs only in the copyright header line — except
`Objects/SoundFontSynthesis/AWE32_1MB.h`, where both repos carry a stub
for the removed GM SoundFont with different wording. Consequences:

- Our AudIO is not an older/newer revision than the sibling's; the
  Phase 1–2 fixes and reconstructions are not superseded by anything
  in 0716-AAF.
- The two open example crashes (BasicExample `SFSynth::setParameter`,
  Subdesign `getObject`) exist identically in the sibling copy — no
  upstream fix to borrow; they're ours to fix.
- 0716's stub header documents the intended SoundFont workflow:
  regenerate `AWE32_1MB.h` from a `.sf2` you have rights to
  (`xxd -i`), keeping the `soundfont`/`soundfontsize` names — a usable
  route for the content pipeline. Its `AudIO/Samples/README.md`
  confirms the demo drum samples were likewise stripped as
  non-redistributable ("put your own `.wav` files here").
- `ignite_constants.h` is that product's UI/sequencer constants, not
  an AudIO engine-constants module — it cannot validate our
  reconstructed `AudIO_constants` values.
