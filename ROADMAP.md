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

## Phase 2 — "The simulator lives" (nearly done — Oct 28, 2026)
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
- [ ] Gate: `KT_KickEM` links, launches under Xvfb, and renders the pad
      grid. (All TUs compile and link blockers are fixed; final link +
      launch test are the last steps.)

## Phase 3 — "The full app on a desktop"
- Build the real processes (`kick_main`, `kick_gui`, `kick_audio_engine`,
  `kick_device_control`) for x86-64 Linux; run them together against the
  simulator as the hardware peer.
- Gate: GUI boots, audio engine answers IPC, a pad press in the simulator
  makes sound through AudIO.

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
