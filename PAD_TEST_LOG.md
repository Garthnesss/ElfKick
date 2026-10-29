# Pad-press end-to-end test log — ElfKick

Date: Oct 29, 2026 · VM: Ubuntu, gcc 13, 2 cores, root
Goal: the last Phase 3 gate item — a **pad press in the simulator**
(KT_KickEM) producing **sound activity through AudIO**, traced hop by hop:
pad press → simulator event → PktMgr packet → device control → IPC →
audio engine/model → audio output activity.

**Overall verdict: FAIL — the chain breaks at the first hop inside
KT_KickEM, and the break is localised exactly (see Hop 1). No code was
changed; this was a verification task. A small, precisely-scoped fix is
recommended at the end but was NOT applied.**

## Setup (per RUNTIME_LOG.md recipe, no deltas needed)

- Factory content already in place from the runtime investigation:
  `/app/assets` (theme), `/app/demos/Demo 1`, `/app/templates/Template 1`,
  `/home/Kick/{projects,logs,prefs}`, `/data/sounds`.
- Supervisor set from `build-app/system/` via `./KickMain` on Xvfb :99
  (1920x1200), `ALSA_CONFIG_PATH=$PWD/asound-null.conf`. All four
  children (KickUserInterface, KickAudioEngine, KickAudioModel,
  KickDeviceControl) spawned with their IPC configs and stayed alive.
- `KT_KickEM` (build-kick/) on Xvfb :100, window 0x200008, fullscreen.
- Injection tool: `xdotool` (installed via apt for this test) — real X11
  input into the simulator window, not code-level signal injection.
- Screenshots in this repo: `pad-test-sim-before.png` (home screen),
  `pad-test-sim-after-tile-click.png` (loading spinner),
  `pad-test-sim-after-load-wait.png` (Demo 1 project view),
  `pad-test-control-browse.png`, `pad-test-pad0-*.png`.

## Test sequence and observations

1. **Baseline.** Simulator home screen renders fully (Demo 01 /
   Template 01 tiles, panel, themed GUI in the centre cut-out).
   Supervisor GUI on :99 also renders (pad-test-gui-before.png).
2. **Open the demo project (GUI-internal input proof).** xdotool click
   on the Demo 01 tile (800,175) in the simulator window → loading
   spinner → project view opens: header "Demo 1", Sequence/Track/
   Program tabs. So X injection works and the GUI processes its own
   internal input. **But the project is empty** — no sequences, tracks
   or programs. The factory demo *content* was not part of the public
   release; `/app/demos/Demo 1` contains only tile artwork, so
   `loadOrCreate` created an empty project. (This matters at Hop 4.)
3. **Pad press, held.** mousedown on Pad 0 (125,930), held 0.4 s,
   screenshot while held, on release, and +1.2 s: **all frames
   pixel-identical to the pre-press frame (ImageMagick AE = 0)** — no
   pad light, no screen change. KT_KickEM's stdout log unchanged
   (513 bytes of network-probing noise only).
4. **Control: BROWSE button click** (867,688, a simulator-panel hardware
   button). Diff vs baseline: 10,545 px, bbox 217x58 at (695,659) —
   **confined to the panel's own button cluster** (the button lit
   locally; its press underline later expired, a 53x2 strip — the only
   other diff seen). The centre GUI screen did not react at all.
5. **Control: idle noise floor.** Two captures 0.3 s apart with no
   input: AE = 0. The UI is perfectly static when untouched, so any
   change would have been attributable — there was none from pads.
6. **More pads.** Pad 0 (full click), Pad 5 (272,825): same null result.

## Code-level trace (why nothing happens)

The intended in-app chain, from the sources:

- Panel pad → `KickSimulatorButton::padStateChanged` →
  `triggerHwEvent` lambda (`Kick/apps/kickem/simulator/kick_Simulator.cpp:69`)
  → `impl->hwInputReceived(SIM_PAD_ID_BASE + pos, {state, 127})`
  on the panel's `sim::SimulatorIntrefaceImpl` singleton.
- SIM_BUILD wrapper (`Kick/modules/input_events/input_events.cpp:311-347`,
  the Phase 2 repair): connects `simInterface->hwInputReceived` → wraps
  into `CPktMgr::HWMsg_t` (pads → `EN_PKT_TYPE_PAD`/`EN_PAD_STATE`,
  data {pad, state, 127, 0}) → emits
  `Controls::InputEvents::dispatchHardwareEvent(msg)`.
  **This connect happens inside `Controls::InputEvents::initialize()`.**
- A bridge then forwards `dispatchHardwareEvent` →
  `contracts::DclSignals::hardwareInputTriggered` — in the combined app
  that's `KickDeviceAdapter` (`Kick/apps/kick/kick_device_adapter.h:42`);
  in the multi-process set it's KickDeviceControl's publisher → IPC →
  each process's `DclBaseMessageHandler`.
- `kick_ui::KickHome` connects `DclSignals::hardwareInputTriggered` →
  `KickHome::handleCommand` (`kick_AppHome.cpp:173,335`), which routes
  pad events toward the model/engine for triggering.

**The break:** `grep` over the whole tree shows
`Controls::InputEvents::initialize()` has exactly three callers —
`apps/kick` (adapter ctor), `apps/kick_device_control` (its main) and
`modules/_playback`. **KT_KickEM is none of them**; its
`KickApplication::initialise` only creates the main window, and no file
under `Kick/apps/kickem/` references InputEvents at all. Therefore in
the simulator binary:

- the SIM_BUILD wrapper is **never connected** — the panel emits
  `hwInputReceived` into a signal with zero slots;
- `dispatchHardwareEvent` **never fires** (its only other producer is
  the serial path, also started by initialize());
- the dispatch→DclSignals bridge is likewise absent (it lives in
  apps/kick / kick_device_control, neither compiled into kickem).

This also explains the Phase 2/3 observations (panel buttons light
locally; the GUI never reacts to panel input) and matches upstream's
own puzzled comment in input_events.cpp about the simulator-interface
instances "somehow clearing the connected signals". The Phase 2
wrapper code itself is correct — it is simply unreachable in kickem.

A gdb corroboration was attempted (disposable KT_KickEM instance on
Xvfb :101): file:line breakpoints never armed (the LTO binary's line
info doesn't resolve those CUs by name in gdb), and a function-name
breakpoint run was inconclusive, so no runtime hit/miss is claimed
from gdb — the verdict rests on the behavioural evidence above plus
the exhaustive caller grep, which agree completely.

## Verdict per hop

| Hop | Verdict | Evidence |
|---|---|---|
| X click → simulator pad component | ✅ WORKS | Same injection loads the project via the tile and lights panel buttons locally; pads are wired `padStateChanged → triggerHwEvent → impl->hwInputReceived` in code |
| Simulator event → HWMsg_t wrapper (input_events SIM_BUILD) | ❌ **BREAKS HERE** | Wrapper is connected only inside `InputEvents::initialize()`, which KT_KickEM never calls (3 callers tree-wide, none in kickem) |
| HWMsg_t → DclSignals::hardwareInputTriggered | ❌ Also absent in kickem | Only bridge is `KickDeviceAdapter` (apps/kick) / the device-control IPC publisher — neither is in the simulator binary. (In the supervisor set this hop IS wired, but that set's hardware source is the stubbed serial port — no live pad source there either.) |
| DclSignals → KickHome::handleCommand → model/engine trigger | ⚠️ Present but never exercised | AppHome connects it at construction (kick_AppHome.cpp:173); no event ever arrives |
| Engine → AudIO sound activity | ⚠️ Untestable until events flow; doubly blocked | Demo project is empty (factory demo content not in the release — no tracks/programs to trigger), and the VM has no sound device (ALSA null), so even a triggered voice would only be observable via meters/logs |

**Gate verdict: NOT PASSED.** A pad press in the simulator produces no
sound activity through AudIO. The failure is not in AudIO, the engine,
the IPC mesh, or the Phase 2 packet wrapper — it is a single missing
initialisation in the simulator app: upstream's kickem never powers
its own hardware-event input path.

## Recommended fix (NOT applied — verification task)

In kickem's app initialisation (mirroring `KickDeviceAdapter`):
call `Controls::InputEvents::initialize()` and connect
`InputEvents::dispatchHardwareEvent` →
`contracts::DclSignals::hardwareInputTriggered`. Expected scope: a
few lines in `Kick/apps/kickem/Main.cpp` (plus linking nothing new —
contracts/ipc/input_events are already in the target). After that,
re-run this test; the next likely blocker is content (an empty demo
project has nothing to trigger — a project with a drum program is
needed to observe a voice), and after that, meter/log-level evidence
of the trigger, since audio itself is unobservable on this VM.

## Cleanup state

Disposable gdb/Xvfb :101 instances were killed. The staged system
(supervisor set on :99, simulator on :100) was left running.
No source files were modified.
