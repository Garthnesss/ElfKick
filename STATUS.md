# ElfKick — Status

**Last updated:** 2026-10-28
**Phase:** 1 mostly done, 2 current (see ROADMAP.md). Repo created by Garth
(Garthnesss/ElfKick, public); this import is the first real commit.

## Where things stand
- **Upstream mapped.** MusicTribeOpenSource/0700-AAM @ `2719418` cloned and
  read: JUCE 7.0.12 app, AudIO engine, PktMgr protocol, Buildroot for
  Variscite i.MX8M Mini. No camera/gesture code exists despite the marketing.
- **AudIO standalone — BUILT + RUNNING (Linux, gcc 13, CMake 3.28).**
  Pristine tree doesn't even configure (case-sensitive `main.cpp`/`core`
  paths). After the 21-file patch in this repo: `AudIO_Core`, `DAW_Core`,
  `AirWindows_plugins` and all 4 console examples compile and link.
  - `DynamicPatch` — **RUNS**: continuous meter output (silent graph, engine
    + device thread + meters alive), survived timeout kill.
  - `DawExample` — **RUNS**: survived timeout kill (needs
    `AUDIO_DAW_EXTENSIONS=1`, now set for the standalone DAW lib).
  - `BasicExample` — **BUILDS, CRASHES at runtime**: starts, prints memory
    + console help, then aborts ("terminate called without an active
    exception"). Prime suspect is the only bare `throw;` in the tree,
    `SFSynth::setParameter`'s default case. An earlier crash in the same
    program (null `_tsf` in `SFSynth::prepareToPlay`, caused by the stubbed
    GM SoundFont) is **fixed** (null guard, ASan-verified trace).
  - `Subdesign` — **BUILDS, CRASHES at runtime**: null deref in the example
    itself (Main.cpp:56) — `getObject` returned null for a just-created
    SineWave in a design containing sub-designs. ASan-verified; not yet fixed.
  - Running any example on a machine with no sound card needs
    `asound-null.conf` (the vendored minisdl ignores `SDL_AUDIODRIVER=dummy`
    and null-derefs in its ALSA thread when no card exists).
- **Kick simulator (KT_KickEM) — CONFIGURES, DOES NOT BUILD (yet).**
  Pristine configure dies cloning AudIO from a placeholder example.com URL.
  With local-source overrides + reconstructed glue (`juce_modules.cmake`,
  `AudIO_constants`/`AudIO_juce` stubs, PktMgr JUCE module wrapper,
  fetched `juce_serialport`, kickem config.h fix) the **full configure
  succeeds**. The build reaches ~50% (JUCE, PktMgr, common, most UI modules
  compile), then fails in `apps/kickem/simulator/kick_Simulator.cpp`:
  ~1,900 error lines rooted in references to a module API that isn't in the
  published tree (`mt::Kick::PadAction`, `TOTAL_DRUM_PADS`, `Displays`, …).
- **Hardware**: no KICK unit; price/availability unconfirmed. Nothing
  hardware-dependent is claimed anywhere in this repo.

## Decisions made
- Fork as **ElfKick**, public, AGPL preserved; canonical home
  `~/Projects/ElfKick` on Garth's Mac, pushed via Mac git. (2026-10-28, Garth)
- Buildroot `Linux/` tree **not vendored** (unmodified upstream, ~9k files);
  JUCE fetched by CPM; `juce_serialport` fetched by script, not vendored
  (unlicensed in upstream's copy). (2026-10-28)
- Reconstructed private-repo pieces are **stubs marked RECONSTRUCTED**, kept
  as small as the build allows, and grown only when the compiler demands a
  symbol. (2026-10-28)
- AudIO fixes kept **minimal and itemised**; pristine upstream clone is never
  edited, so fixes re-derive as a clean patch. (2026-10-28)

## Open decisions (need Garth's call)
1. **Simulator strategy** — reconcile kickem against the published modules
   symbol-by-symbol (Phase 2 as written), or skip the simulator and go
   straight for the real processes with a fake-MCU harness (Phase 3 first)?
2. **How closely to track upstream** — rebase fixes onto new upstream drops
   as they come, or freeze on `2719418` until Phase 3? (No upstream activity
   since Oct 26 release as of this writing; 14 commits total, mostly README.)
3. **macOS build** — worth attempting the AudIO examples + simulator on the
   Mac (Apple Silicon) next, or stay Linux-only until the simulator links?

## Next actions
- [ ] Muse: BasicExample `setParameter` caller hunt (Phase 1 leftover).
- [ ] Muse: Subdesign `getObject` registry bug (Phase 1 leftover).
- [ ] Muse: smoke script for the examples (build + ALSA-null timeout runs).
- [ ] Garth: simulator strategy call (Open decision 1) when he's ready.
