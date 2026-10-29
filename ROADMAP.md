# ElfKick — Roadmap

Phases are gated on verification, not dates. A phase is done when STATUS.md
can state its claim with a run behind it.

## Phase 0 — "What's actually in the box" ✅ (Oct 28, 2026)
- Read the upstream release end to end; map hardware, processes, protocol.
- REA-Results checked (standing rule): no prior Behringer/KICK work.
- Deliverable: BUILD_FINDINGS.md + the four canonical docs. **Done.**

## Phase 1 — "AudIO runs on Linux" ✅ mostly (Oct 28, 2026)
- Standalone AudIO build repaired (21-file patch, audio-linux-fixes.patch).
- Libraries + 4 console examples build; DynamicPatch + DawExample run.
- Remaining in this phase:
  - [ ] BasicExample abort: find who calls `SFSynth::setParameter` with an
        out-of-enum index after device start (only bare `throw;` in the tree).
  - [ ] Subdesign crash: `Design::getObject` returns null for a just-created
        object in a design that has sub-designs — registry/id bug hunt.
  - [ ] Give the examples a CI-style smoke script (build + timeout run +
        ALSA null) so regressions are one command.

## Phase 2 — "The simulator lives" ✅ (Oct 28, 2026)
- [x] Reconcile `apps/kickem` with the published module API: every missing
      symbol resolved per symbol — renamed (`mt::Kick::*` → contracts /
      `BtnLedSequence` / `PadSequence` / `BtnState`), API-migrated (sigslot
      `operator()`, JUCE 7 Displays, `kick_ui::KickHome`), or reimplemented
      (`getPadColourFromId` as a local colour map). Full log:
      SIM_BUILD_LOG.md.
- [x] `juce_serialport` on Linux: repo-kept fail-safe stub backend
      (`juce_serialport_Linux.cpp`) — the simulator has no real MCU to
      talk to; a termios backend stays an option if a real port is needed.
- [x] AudIO_constants / AudIO_juce stubs replaced with real reconstructed
      content (constants inferred from call sites; recorder/looper/device
      classes adapted over the published `DRecorder`).
- [x] Gate: `KT_KickEM` links, launches under Xvfb, and renders the pad
      grid. (23 MB binary, alive the full 15 s timeout run, screenshot
      in repo. The launch also shook out an upstream crash —
      RecentProject empty-vector deref with no factory content —
      fixed with a bounds guard.)

## Phase 3 — "The full app on a desktop" ✅ mostly (Oct 28, 2026)
- [x] Build the real processes for x86-64 Linux: all six targets link
      (`Kick`, `KickUserInterface`, `KickAudioEngine`, `KickAudioModel`,
      `KickMain`, `KickDeviceControl`) — three CMake link fixes total;
      see PHASE3_BUILD_LOG.md.
- [x] Run them together against the simulator as the hardware peer:
      KickMain-supervised set + KT_KickEM, all six alive, full IPC mesh
      connected. The GUI's black home screen was a missing-install
      issue (the UI themes itself from `/app/assets`, hardcoded) —
      with the repo's `Kick/assets` tree in place it renders the real
      Projects screen. See RUNTIME_LOG.md.
- Gate: GUI boots **and draws** ✅, audio engine answers IPC ✅,
  a pad press in the simulator makes sound through AudIO — **the one
  item still unverified**.

## Phase 5 — "The emulator: app platform + content pipeline" (Garth's goal, Oct 28, 2026)
The fork's purpose: pre-make things for the device on a desktop rig that
reproduces its layout and runtime. Two deliverable lines:

**App platform (first)**
- [ ] One-command emulator launcher (`scripts/run-emulator.sh`): stages
      the binaries, installs the asset tree, starts the supervisor set
      + simulator at the true layout (GUI at 1024×600).
- [ ] APP_PLATFORM.md: the app surface, documented from the code —
      screen contract (1024×600, theme/assets), input contract
      (contracts/DclSignals hardware events), output contract (pad LED /
      display commands), how a process joins the KickMain-supervised
      set (co-location + JSON config), and how to reach AudIO.
- [ ] A starter template app: minimal process that shows its own
      screen, lights pads, and reacts to pad/knob input — the "hello
      world" every later ElfKick app forks from.
- Gate: the starter app runs inside the rig, a simulator pad press
  visibly changes its screen, and it can drive a pad LED back.

**Content pipeline**
- [ ] Reverse the project/program file format from the loader code
      (projectManager / ValueTree serialisation) well enough to
      *author* files the stock software opens.
- [ ] A first hand-made content pack: one project with a drum program
      built on AudIO synth voices (not samples — the factory samples
      were never released).
- Gate: the stock GUI loads our project, and (with the Phase 3 input
  fix in) a simulator pad press triggers a voice — meter/log evidence
  on this sound-card-less VM, ears when hardware exists.

## Phase 4 — "On the metal" (blocked on hardware)
- Cross-build the Buildroot image + app for the i.MX8M Mini (SDK from the
  upstream Linux tree), boot on a real KICK if/when a unit exists.
- Nothing in this phase is scheduled; it waits for hardware and Garth's go.

## Explicitly later (not this roadmap)
- New features/instruments on top of AudIO (the engine is the interesting
  reusable part — a desktop plugin host for AudIO designs is a possible
  spin-off project, not an ElfKick phase).
- The AI camera. No code exists to build on.
- Any upstreaming/PRs to MusicTribeOpenSource — only if Garth asks; ElfKick
  is maintained in the open either way.
