# Pad-gate log — ElfKick

Date: Oct 29, 2026 · VM: Ubuntu, gcc 13, 2 cores, root
Task: apply Garth's pad-gate decision (Oct 28, 2026: *in the emulator,
pads always emit*), generate the first content-bearing project, rebuild
once, and run the full pad→voice verification (the last Phase 3 gate
item). Builds on PAD_TEST_LOG.md (break localised), PAD_FIX_LOG.md
(input bridge fixed; pads then blocked only by the widget StateOff
gate) and PROJECT_FORMAT.md (content blueprint).

## Step 1 — Pad gate as a runtime switch (deliberate ElfKick deviation)

Garth refined the decision mid-task: **both** pad behaviours must stay
available as options down the line. So the gate is a runtime switch,
not a hard removal.

Mechanism: environment variable **`ELFKICK_PADS`**, read once
(function-local static, resolved at panel construction so the active
mode is logged at startup):

- `ELFKICK_PADS=always` — **default** (also when unset or any other
  value): pads emit `padStateChanged` on click regardless of
  `PadButtonState`. Matches the real device, whose MCU sends pad
  packets unconditionally.
- `ELFKICK_PADS=upstream`: the exact original behaviour — the
  condition in `mouseDown`/`mouseUp` reduces to the original
  `if (state == PadButtonState::StateOff) return;` and nothing else
  changes, so the code path is behaviour-identical to upstream.

Change (only file: `Kick/apps/kickem/simulator/kick_Simulator.cpp`):

- Anonymous-namespace helper `elfKickPadsAlwaysEmit()` near the top of
  the file: reads `ELFKICK_PADS` once into a static, prints one
  startup line, e.g.
  `ElfKick: ELFKICK_PADS=(unset) -> pad gate mode: always (pads emit regardless of state)`.
- `KickSimulatorButton::mouseDown` / `mouseUp` (~original lines 35/44):
  early return becomes
  `if (! elfKickPadsAlwaysEmit() && state == PadButtonState::StateOff) return;`
  — in upstream mode this is the original gate verbatim; in always
  mode it never returns early. `paint()` is untouched: StateOff pads
  still *look* off in both modes.
- `SimulatorButtonGrid` ctor calls the helper once so the mode line is
  emitted at startup.

**Scope check (other controls):** grep over `Kick/apps/kickem/simulator/`
for `StateOff` / `PadButtonState` suppression — the StateOff gate exists
*only* in `KickSimulatorButton::mouseDown/mouseUp` (and visually in
`paint()`). The transport buttons and qlink knobs
(`kick_SimulatorControls.cpp`), the main-control/bank buttons and the
central encoder (`kick_SimulatorMainControl.cpp`) all call
`impl->hwInputReceived(...)` directly with no state suppression —
consistent with PAD_FIX_LOG, where BROWSE/MAIN already propagated.
Nothing else was changed.

## Step 2 — Content generator (`scripts/make_test_project.py`)

Implements PROJECT_FORMAT.md §5–§6 (the recon's recommended route).
Idempotent; stdlib only (`wave`/`struct`/`math` for the sample).
Writes into `/home/Kick/projects/ElfTest01/`:

- `ElfTest01.kickPrj` (3,638 bytes) — ROOT + `FILEMANAGER` with one
  FILE asset + one Drum `PROGRAM` (`type="0"`, `name="Elf Drums"`,
  **`id="ElfProg1"` authored explicitly** — an empty id is randomised
  on load and would break the track link) whose `CLIPS` cover keyNotes
  **36–51** (bank A), each clip min=max=keyNote with one `LAYER` whose
  `source` is the wav path; plus `SEQUENCES > SEQUENCE > TRACK` with
  `program="ElfProg1"`.
- `kick.wav` (38,444 bytes) — synthesised kick: pitch-swept sine
  150→45 Hz with exponential decay, 0.4 s, 48 kHz / 16-bit mono. The
  FILE `path` and every LAYER `source` are the *same string*
  (`/home/Kick/projects/ElfTest01/kick.wav`) — the path is the
  slice-lookup key, so they must match character-for-character.
- `ElfTest01.png` — home-screen tile, copied from the shipped
  `Kick/demos/Demo 01/Demo 01.png`.

A pristine copy of the generated `.kickPrj` was kept aside
(`/tmp/padgate/ElfTest01.generated.kickPrj`) for the autosave diff in
Step 3.

## Step 3 — Rebuild + retest

### Rebuild

- One TU recompiled (`kick_Simulator.cpp`), then the LTO link (128
  serial LTRANS jobs; ~42 min wall on this VM, which was simultaneously
  running other projects' builds and the idle supervisor set).
  **`[100%] Built target KT_KickEM`, 0 errors** — binary
  `build-kick/apps/kickem/KT_KickEM_artefacts/Release/KT_KickEM`,
  23,425,040 bytes. Log: `build-kick/padgate-build.log`.
- Environment notes: the X11 `-dev` packages wiped by the earlier VM
  restart had been restored by the OS package replay in the meantime
  (dpkg-verified); xdotool/ImageMagick/xwd still live in `/tmp/tools`
  (need `LD_LIBRARY_PATH` + `MAGICK_CONFIGURE_PATH`, see
  `/tmp/padgate/env.sh` this session). One hazard cleaned up en route:
  an orphaned compiler from the first, refinement-killed build attempt
  was racing the restarted build on the same `.o` path — killed by PID.

### Retest rig

Staged system relaunched per RUNTIME_LOG (KickMain set on Xvfb :99,
KT_KickEM on :100, `/app` layout intact). Startup line in the KT_KickEM
log confirms the switch resolves at startup:
`ElfKick: ELFKICK_PADS=(unset) -> pad gate mode: always (pads emit regardless of state)`.

### Round 1 — pads deliver, note misses the clips (content bug, fixed)

- Opened ElfTest01 via the simulator GUI (File Browser →
  /home/Kick/projects → ElfTest01.kickPrj). It loaded exactly as
  authored: header "ElfTest01", track 1 **Drums / Elf Drums**,
  bottom strip Sequence 01 / Drums / Elf Drums
  (`pad-gate-project-loaded.png`'s twin in this round,
  `pad-gate-before.png`). The sequence's
  `Sequence 01/Sequence 01.mid` was auto-created on load, as the recon
  predicted.
- Pad 0 press (0.4 s hold): log gained the handleCommand traces
  `Data[0] From DCL:0 cmd From DCL : 0 type From DCL : 3 … In Recent
  Prj register handle command` for press **and** release — the widget
  gate is gone; hops (a) emit and (b) DCL→KickHome delivery pass. Held
  frame vs before: AE 2,042 (pad highlight).
- But no voice could trigger: tracing the delivery path in source —
  `AppMainmode::handleCommand` (kick_AppMainMode.cpp:396ff) builds
  `noteOn(1, padNo + bankOffset, velocity)` with
  `getBankOffset() = 16 × (bank − 'A')` = 0 for bank A, and neither
  `MTTrack::processIncomingMidiMessage` (only track/sequence
  transpose, both 0) nor `MTIProgram::noteProcess` nor
  `DrumProgram::noteOn` (exact `getDrumClip(Note)` lookup) applies any
  further transposition. **Pad 0 therefore delivers note 0** — the
  pad-note space is 0-based across the 8 banks (0–127), and the
  PROJECT_FORMAT §3 assumption of an MPC-style base note 36 (drawn
  from the pad-*performance* defaults, which govern a different path)
  is wrong for physical pads. `iSampleCreator(Note)` confirms clips
  are created keyed by exactly the arriving note. Round 1's clips at
  36–51 were simply never addressed.
- **Fix applied to the generator only** (no app code):
  `FIRST_PAD_NOTE 36 → 0`, comments corrected; project regenerated
  (clips now keyed 0–15), rig relaunched, project reopened.

### Round 2 — full chain to a rendered voice

Frames: `pad-gate-project-loaded.png` (before), `pad-gate-pad0-held.png`,
`pad-gate-pad0-after.png`, `pad-gate-mixer-press.png`,
`pad-gate-progmeter-lit.png`, `pad-gate-progmeter-decayed.png`.

- **(a) Simulator emits:** Pad 0 mousedown now reaches the app; held
  frame AE 1,328 vs loaded frame (pad highlight while held).
- **(b) DCL → KickHome:** handleCommand trace pair for press/release
  (`Data[0] From DCL:0 … type From DCL : 3 … register handle command`),
  log +1,544 bytes around the press.
- **(c) Track/program trigger:** the current screen
  (AppMainmode) converts the pad to `noteOn(1, 0, 127)` →
  `ae.addLiveMidiMsg(selected, …)` → selected track Drums →
  `noteProcess` → `DrumProgram::noteOn(0)` → clip 0 (present after
  the generator fix). Visible corroboration: the track row's meter
  lit at the press — with a caveat: that widget binds to the track's
  **MTMidiMeter** (`kick_AppTrackComponent.cpp:169` →
  `getTrackMeter`), which reloads on any delivered note, clip or not.
  It proves delivery to the track, *not* the voice. The voice proof
  is (d).
- **(d) Voice renders (audio activity):** in the Track Mixer
  (Programs) screen, the Elf Drums row meters are fed by
  `pProg->readInMeter()` / `readOutMeter()`
  (`kick_audio_track_mix.cpp:289-292`) — the synth object's meters,
  updated only inside `ISynthObject::process(buffer)`
  (iSynthObject.cpp:207), i.e. the real render path. Baseline black;
  **150 ms after Pad 0 mousedown: stereo bar at ~75 % scale**
  (crop AE 2,106); **at 450 ms: decayed to ~zero** — tracking
  kick.wav's 0.4 s envelope. A DrumVoice played the generated sample
  through the program bus. (The master-row meter crop showed no
  change at 150 ms; not needed given the program meters + decay
  signature.)

### Switch A/B — upstream mode

KT_KickEM relaunched with `ELFKICK_PADS=upstream` (supervisor set
left running): startup line reads
`ElfKick: ELFKICK_PADS=upstream -> pad gate mode: upstream (StateOff gate active)`.
With ElfTest01 loaded (title verified in frame), a 0.4 s Pad 0 hold
produced **AE = 0 and zero new log bytes** (no handleCommand trace) —
pads are inert again, exactly the original upstream behaviour. The
switch works both ways; default (unset) is `always`.

### Autosave / golden-reference check (bonus)

No normalised rewrite occurred: through load, presses, mixer visits
and even a confirmed GUI edit (track LEVEL 100 → 64, Undo armed), the
on-disk `.kickPrj` stayed byte-identical to the generator output
(md5 `667897d4…`, still 3,608 bytes) for 15+ s after the edit —
autosave (1.5 s debounce per the recon) did not fire in this topology,
or the file is owned by a model instance that doesn't persist from
the simulator session. The generated file therefore remains the
on-disk truth (itself a useful content-pipeline fact), and the
normalised golden reference is still outstanding — follow-up: capture
it via an explicit save path or the supervisor-GUI topology.

### Regression

KT_KickEM headless launch check (Xvfb :101, null-ALSA, `timeout 15`)
→ **exit 124**, mode line present. Supervisor-set binaries untouched
by this task (kickem only).

## Verdict — Phase 3 gate: **PASSED**

**Pad press → triggered voice, end to end, in the emulator:**
simulator pad (gate now runtime-switched, default always-emit) →
SIM_BUILD wrapper → `dispatchHardwareEvent` → KickDeviceAdapter →
`DclSignals::hardwareInputTriggered` → `KickHome::handleCommand` →
AppMainmode noteOn → selected track → DrumProgram clip →
**DrumVoice renders the generated kick.wav**, evidenced by the
program's render-fed output meters lighting at 150 ms and decaying
with the sample's envelope by 450 ms — on content authored entirely
by `scripts/make_test_project.py`. The one failure found en route was
in our generated content (clip notes 36–51 vs the device's 0-based
pad-note space), fixed in the generator, not the apps. Remaining
honest limits: the VM has no sound device, so "sound" is proven by
render-path metering, not by ear; and the autosave golden reference
is still to be captured.
