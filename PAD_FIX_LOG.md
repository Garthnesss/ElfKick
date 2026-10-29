# Pad-press fix log — ElfKick

Date: Oct 29, 2026 · VM: Ubuntu, gcc 13, 2 cores, root
Task: apply the fix localised by PAD_TEST_LOG.md — KT_KickEM never calls
`Controls::InputEvents::initialize()`, so the Phase 2 SIM_BUILD wrapper in
`input_events.cpp` (SimulatorInterface::hwInputReceived →
dispatchHardwareEvent) is never armed, and no bridge forwards
dispatchHardwareEvent → `contracts::DclSignals::hardwareInputTriggered`
in the simulator binary. Panel input emitted into a signal with zero slots.

## Fix design (mirrors the existing patterns)

Reuse `KickDeviceAdapter` itself (`Kick/apps/kick/kick_device_adapter.h`)
instead of duplicating its logic: it is a header-only class whose ctor does
exactly the two missing steps —

1. `Controls::InputEvents::initialize()` (in SIM_BUILD this grabs the
   SimulatorInterface via `Factory<SimulatorInterface>::makeObject(0)` and
   connects the hwInputReceived → HWMsg_t wrapper → dispatchHardwareEvent),
2. connects `dispatchHardwareEvent` → `DclSignals::hardwareInputTriggered`
   (plus the reverse DclSignals::sendCommandToDcl /
   createAndSendCommandToDcl → `InputEvents::sendMessageToHardware`, i.e.
   UI→panel LED/command traffic), and the keyboard-signal bridge.

apps/kick (the combined app) uses it exactly this way: a
`std::unique_ptr<KickDeviceAdapter>` member created in `initialise()`
(apps/kick/Main.cpp:134,245). kickem now does the same. No new link
libraries were needed — `input_events` (incl. the SIM_BUILD wrapper) and
PktMgr are already compiled into KT_KickEM (verified:
`build-kick/apps/kickem/CMakeFiles/KT_KickEM.dir/__/__/modules/input_events/input_events.cpp.o`),
and `contracts`/`ipc` were already linked in Phase 2.

**Ordering constraint (the subtle part):** the adapter is created AFTER
`MainWindow` in kickem's `initialise()`, deliberately unlike apps/kick
(where order is irrelevant — no simulator singleton there).
`sim::SimulatorIntrefaceImpl` enforces a singleton via a custom
`operator new` that returns the existing instance, after which the
constructor re-runs on that same object — re-creating its
`hwInputReceived` signal and dropping any connections already made to it.
(That is the mechanism behind upstream's puzzled comment in
`input_events.cpp`: factory creation "somehow clearing the connected
signals".) The panel (which performs the last `new SimulatorIntrefaceImpl`
calls) is built inside MainWindow, so once MainWindow exists,
`initialize()`'s factory instance IS the panel's instance and its
connection survives. Creating the adapter first would let the panel's
construction silently disconnect the wrapper again.

## Changes

- `Kick/apps/kickem/Main.cpp` — include `../kick/kick_device_adapter.h`;
  add `std::unique_ptr<KickDeviceAdapter> deviceAdapter` member; create it
  in `initialise()` after `mainWindow`; reset it in `shutdown()` before
  the window. ElfKick comments in place explain the what and the ordering.
- (No CMakeLists change: the adapter is header-only and every module it
  needs was already in KT_KickEM's link graph.)

## Rebuild

- Main.cpp (the only changed TU) compiled clean on the first attempt —
  `Main.cpp.o` (1.7 MB) with zero errors; the adapter header and all its
  includes resolve in kickem's existing module graph, as expected.
- Environment incident (not a code issue, same failure mode as Phase 2/3):
  the VM restarted mid-link, killing the build, the staged system and
  both Xvfb servers, and wiping the X11 `-dev` packages again. The OS
  package replay started but its `apt-get update` sat for 10+ minutes,
  so the missing headers were restored from the local apt cache instead:
  the `-dev` .debs (libxrandr/libxinerama/libxcursor/libxi/libxfixes/
  libxcomposite/libxdamage) were extracted with `dpkg-deb -x` into
  `/tmp/x11dev` (dpkg database untouched; runtime .so's were still
  present) and the build was run with
  `CPATH=/tmp/x11dev/usr/include LIBRARY_PATH=/tmp/x11dev/usr/lib/x86_64-linux-gnu`.
  With that, the two JUCE TUs that needed the headers
  (juce_gui_basics, juce_gui_extra) compiled and the build reached the
  LTO link stage (128 serial LTRANS jobs).
- Link completed: **`[100%] Built target KT_KickEM`, 0 errors** —
  `build-kick/apps/kickem/KT_KickEM_artefacts/Release/KT_KickEM`, 23 MB
  (same size class as the Phase 2 binary). Build log:
  `build-kick/padfix-build4.log` (attempts 1–3 in padfix-build*.log were
  the restart / missing-header failures above).
- The restart had also rolled back `/app` and dropped the test tools
  (xdotool, ImageMagick, xwd — all apt-installed in earlier sessions).
  `/app` was recreated exactly per RUNTIME_LOG (assets tree + Demo 1 /
  Template 1 tile art + /home/Kick/projects etc.), and the tools were
  extracted from the apt cache into `/tmp/tools` the same way as the
  headers (xdotool additionally needed libxtst6, convert needed
  liblqr-1-0; both cached).

## Regression check

- KT_KickEM launch check (Phase 2 method): Xvfb :101, null-ALSA config,
  `timeout 15` → **exit 124** — the fixed binary launches and stays alive
  the full 15 s, no crash from the new adapter initialisation.
- Supervisor set: untouched by this fix (kickem only) and not rebuilt;
  relaunched from `build-app/system/` for the retest — KickMain + all
  four children + KT_KickEM ran concurrently throughout.

## Retest (xdotool, two Xvfb servers — :99 supervisor GUI, :100 simulator)

Same protocol as PAD_TEST_LOG, frames in `pad-fix-*.png`, KT_KickEM
stdout captured to a log and size-watched around each press.

1. **Tile click (GUI-internal input):** Projects home → Demo 01 tile →
   project screen opens (AE 590,812 vs home frame). Same as before. ✓
2. **Panel BROWSE button — NOW PROPAGATES.** In the failed test this
   press changed only the button's own light (AE 10,545, transient) and
   the app log stayed byte-identical. Now:
   - the simulator's centre screen switches to the **File Browser**
     (/home/Kick/projects; Internal / Factory Sample Library / Demos)
     and stays there — AE 515,999 vs the pre-press frame, identical at
     press and +1.5 s (persistent screen change, not a light);
   - the panel's BROWSE button stays lit gold in the after-frame — the
     UI's state feedback reached the panel over the reverse path
     (DclSignals → adapter → sendMessageToHardware → sim);
   - KT_KickEM's log gains the handler trace from
     `kick_ui::KickHome::handleCommand` (kick_AppHome.cpp:337ff — it
     logs every delivered HW message):
     `Data[0] From DCL:10 cmd From DCL : 0 type From DCL : 1 … In Recent
     Prj … In is button … In screen 10 … register handle command`
     (10 = EN_BROWSE).
   - The panel **MAIN** button likewise: handled + traced
     (`Data[0] From DCL:8 … In screen 8 … register handle command`) and
     the screen returned from the browser to the project view.
   So the full chain panel → SIM_BUILD wrapper → dispatchHardwareEvent
   → DclSignals::hardwareInputTriggered → KickHome now fires in the
   simulator, both directions.
3. **Pad 0 press — still no downstream effect, and the remaining break
   is localised one hop earlier, inside the panel widget itself**
   (upstream code, not the bridge):
   - Presses (0.4 s holds, frames held/released/after) on the home
     screen state, project screen, File Browser screen, and again after
     creating content (below): AE = 0 every time, and **no
     handleCommand log line** — significant because handleCommand logs
     unconditionally on entry for ANY delivered message. The pad event
     never reaches the bridge.
   - Why (source-verified): `sim::KickSimulatorButton::mouseDown` /
     `mouseUp` (Kick/apps/kickem/simulator/kick_Simulator.cpp:35,44)
     **return early while the pad's state is `PadButtonState::StateOff`**,
     so an "off" pad never emits `padStateChanged` at all. Pads start
     StateOff and only leave it when the app pushes a SetPadState
     command (`SimulatorButtonGrid::hwMessageReceived`). In this
     content the app never pushed one (see 4), so the pads are inert by
     the widget's own upstream logic — on real hardware the MCU sends
     pad packets unconditionally, and kick_device_control (which calls
     `initialize()` itself) was never broken.
   - This gate is outside the scoped two-step fix (arming the wrapper +
     the DCL bridge); the bridge is proven live by the button traffic
     above, which shares the same wrapper → dispatch → bridge path.
4. **Content-creation attempt (timeboxed, per brief):** through the GUI
   alone — "+" on the Track tab created **Track 1** (and Sequence1);
   "+" on the Program tab opened CREATE PROGRAM; chose **DRUM** →
   **Program 01** on Track 1 (frames pad-fix-plus1.png,
   pad-fix-progplus.png, pad-fix-drumprog.png). Creating the program
   still did not push pad states to the panel (Pad 0 press after:
   AE = 0, no log line) — plausibly states are pushed on entering a
   perform view or on sample assignment; no samples exist to assign
   (factory content was never released), so that route dead-ends here.
5. **Audio hop:** untestable on this VM — no sound device, and the drum
   program has no samples. Unchanged from the pad test's known limit.

## Verdict

**The scoped fix works.** `Controls::InputEvents::initialize()` plus the
`KickDeviceAdapter` bridge (one file changed:
`Kick/apps/kickem/Main.cpp`) restore the simulator's hardware-input
path: panel button presses now provably propagate into the app —
GUI screen changes (BROWSE → File Browser), persistent UI→panel state
feedback, and `KickHome::handleCommand` log traces — where the pad test
recorded total downstream silence. KT_KickEM rebuilt clean (23 MB,
0 errors), launch regression green (exit 124), supervisor set untouched.

**Remaining, separately-scoped issue:** simulator *pad* presses are
still swallowed, but at the panel widget's upstream StateOff gate
(kick_Simulator.cpp:35,44), before the (now-live) input path. Making
pads audition would need either the app to push pad states in this
flow or a deliberate ElfKick change to the widget's gate (emit on
press regardless of state) — a behaviour change to upstream panel
semantics, left as a decision for the parent/Garth rather than folded
into this minimal fix.
