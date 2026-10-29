# Web panel ⇄ ElfKick runtime bridge — design & log

The web groovebox panel in this directory (Garth, Oct 29, 2026: "its
perfect") becomes a front end for the real ElfKick runtime: press a pad
in the browser and ElfTest01's drum program triggers in the actual
Kick processes; press BROWSE and the runtime's LED feedback lights the
web panel. KT_KickEM stays the fidelity reference — this is an
additional front end, not a replacement.

## How hardware input really enters the runtime (traced)

```
MCU ──serial 230400 8N1──► KickDeviceControl ──IPC broadcast──► KickUserInterface
     PktMgr frames          (kick_dcl_main.cpp)   HW_EVENT          (DclBaseMessageHandler)
                                │                                     │
                                │ InputEvents::initialize()           ▼
                                │ HwSerialData (input_events.cpp)  DclSignals::
                                │  · changeListenerCallback parses hardwareInputTriggered
                                │    PktMgr frames ('M','T',…)       │
                                │  · dispatchHardwareEvent            ▼
                                │  · KickHardwareEventPublisher    KickHome::handleCommand
                                ▼    hWMessageToIPCMessage
     ◄──serial──────────────  KickDeviceMessageHandler (HW_COMMAND)
                                InputEvents::sendMessageToHardware → txQueue →
                                CPktMgr::BuildAndTransmitPacket → serial out
```

- `KickDeviceControl` is **not** a SIM_BUILD binary (SIM_BUILD=1 exists
  only in `Kick/apps/kickem/CMakeLists.txt:45`). Its
  `InputEvents::initialize()` takes the hardware path: `HwSerialData`
  opens **`/dev/ttymxc0`** at 230400 baud via `juce_serialport`
  (`input_events.cpp` `HwSerialData::connect()`), gated on
  `juce::File("/dev/ttymxc0").exists()` and a non-empty
  `SerialPort::getSerialPortPaths()`.
- Inbound: bytes → `CPktMgr` frame parser → `HWMsg_t` →
  `dispatchHardwareEvent` → publisher → IPC `HW_EVENT` broadcast →
  GUI's `DclBaseMessageHandler` → `DclSignals::hardwareInputTriggered`
  → `KickHome::handleCommand` — **the same terminal hop the verified
  simulator path lands on** (PAD_FIX_LOG / PAD_GATE_LOG).
- Outbound (feedback): the GUI's `kick_leds` module emits
  `DclSignals::sendCommandToDcl` (setLEDON/OFF, setBank, setPadData,
  setMediaControl, setTempo, …) → `DclIPCInterface` → IPC
  `HW_COMMAND` → DCL's `KickDeviceMessageHandler` →
  `InputEvents::sendMessageToHardware` → PktMgr frame → serial out.
  On the device the MCU turns those frames into lit buttons/pads.
- **The blocker, pre-bridge:** ElfKick's `juce_serialport_Linux.cpp`
  was a fail-safe stub (every op a no-op, `getSerialPortPaths()`
  empty) — on Linux nothing could ever reach DCL's serial path. That
  stub is ElfKick's own repo-kept file (upstream module ships no Linux
  backend at all), so completing it is within the existing interface.

### PktMgr wire format (ComProtocol/PktMgr/PktMgr.cpp)

```
'M' 'T' type dir cmd size data[size-1] crc8
size = dataLength + 1 · CPktMgr frame = size + 6 bytes on the wire
crc  = CRC-8, init 0xFF, poly 0x31, over all preceding bytes
dir  = 0 REQUEST (both directions in practice), 1 RESPONSE
types: 0 SYSTEM · 1 BUTTON · 2 LED · 3 PAD · 4 KNOBS · 5 MIDI · 6 CV · 7 SLIDER
```

One wire subtlety, found by making the numbers add up:
`HwSerialData::changeListenerCallback` slices the stream with
`packetLength = size + 7` — one byte **more** than a CPktMgr frame —
and `BuildAndTransmitPacket` carries a commented-out trailing
`push_back('\n')`. Together they say the MCU-side transmitter appends
a `'\n'` after the CRC (the receiver consumes but never pushes that
byte into the frame parser). The bridge mirrors the MCU: every frame
it sends to DCL is followed by `'\n'`. Frames from DCL have no
trailing byte; the bridge's parser resyncs on `'M','T'` and tolerates
either.

Payload layouts (`contracts/dcl/dcl_contracts_helper.cpp`, getters and
message constructors — the same layouts the SIM_BUILD wrapper produces
for KT_KickEM, which is why the gate-test evidence carries over):

| Dir | type/cmd | data |
|---|---|---|
| in | BUTTON / 0 `EN_BTN_STATUS` | `[BtnLedSequence code, BtnState]` (0 rel, 1 press, 2 long, 3 dbl) |
| in | PAD / 0 `EN_PAD_STATE` | `[pad 0..15, BtnState, velocity, aftertouch]` |
| in | KNOBS / 1 `EN_KNOB_QLINK` | `[qlink 0..3, direction (0 CCW, 1 CW), steps]` |
| in | KNOBS / 0 `EN_KNOB_ROTARY` | `[KnobSequence, direction, steps]` (center = 0) |
| in | SLIDER / 0 `EN_SLIDER_TOUCH_INPUT` | `[value 0..100]` (percent — sim slider range is 1..100) |
| in | SYSTEM / 4 `EN_SYS_CHECK_COMM_STATUS` | `[1]` = comm-init announce |
| out | LED / 0 `EN_LED_SET_SINGLE_LED` | `[BtnLedSequence, PadButtonState (0 off, 1 on…), colourId]` |
| out | LED / 1 blink, LED / 2 set-all | `[id, state, colour]`-ish (forwarded generically) |
| out | PAD / 1 `EN_PAD_SET_SINGLE_PAD` | `[pad, PadButtonState, PadColourId]` |
| out | PAD / 2 set-all-one-colour, 5 colour+state bulk, 8 blink, 9/10 step pads | decoded generically (see bridge) |
| out | SLIDER / 1..3 | `[percent]` → strip LEDs |
| out | SYSTEM / 2 `EN_SYS_COMM_INIT`, 4 `EN_SYS_BPM` | handshake reply / tempo (logged) |

## Injection point — chosen: (a) the serial wire, bridge plays the MCU

**The bridge is a Python process that owns a pty.** `/dev/ttymxc0` is a
symlink to the pty slave; the bridge holds the master. It speaks
PktMgr exactly like the MCU firmware would, in both directions, and
bridges to the browser over WebSocket. To make DCL open the port, the
ElfKick Linux serial stub is completed with a real termios backend
(mirroring the module's own OSX implementation, same header/API).

Why this one:

- **It reuses the verified path end-to-end.** Every hop downstream of
  the serial wire — PktMgr parser, `dispatchHardwareEvent`, the IPC
  publisher, `DclBaseMessageHandler`, `KickHome::handleCommand` — is
  upstream code already proven with the simulator's identical HWMsg
  shapes (PAD_GATE_LOG). The bridge invents nothing downstream; it
  only has to be a well-behaved MCU.
- **Feedback comes back for free.** LED/pad/slider frames the GUI
  sends toward "the hardware" arrive at the pty master; the bridge
  decodes them into `setLed`/`setPad` calls. No other option gets the
  return direction without new code inside a runtime process.
- **Least new surface in the runtime:** one file changes
  (`juce_serialport_Linux.cpp`, ElfKick's own stub → real backend,
  behind the existing module interface). No process gains a new
  responsibility; KT_KickEM and the supervisor topology are untouched.

The alternatives, honestly costed:

- **(b) Bridge process joining the IPC mesh** (publishing HW_EVENT /
  consuming HW_COMMAND as a peer): would need to link `contracts` +
  `ipc`, replicate the DCL connection config and the mt_ipc JSON
  protocol, and re-implement what `KickHardwareEventPublisher` /
  `KickDeviceMessageHandler` already do — strictly more new surface,
  and it bypasses (rather than exercises) the device-control layer the
  emulator exists to model.
- **(c) Driving KT_KickEM's input path** (X11 injection / its
  SimulatorInterface): that's what the gate tests did for *testing*;
  as a product bridge it couples the web panel to a desktop GUI
  process's internals, gives no clean programmatic LED return path
  (feedback lands in the sim's widgets, not on a socket), and keeps
  the heavyweight simulator in the loop for no fidelity gain — the
  serial path *is* the hardware path.

## Mapping — web panel → runtime

Panel facts (build.py / template.html): pads 1..16 with **1 =
bottom-left**; the runtime numbers pads 0..15 with **0 = bottom-left**
(sim grid lays pos 0 at bottom-left, `kick_Simulator.cpp` `resized()`),
and delivers pad note = pad + 16 × bankOffset (PAD_GATE_LOG). So
panel pad *n* → runtime pad *n−1*, direct.

Buttons: the panel's names are the hardware silkscreen; the runtime's
identity is `contracts::DeviceControlContracts::BtnLedSequence`. The
panel emits raw names plus a separate SHIFT event; **shift translation
is the MCU's job** — in-tree reference `kick_SimulatorMainControl.cpp`
does exactly this (its sub-labels match the panel's printed sub-labels
one-for-one), and the bridge mirrors that table: while SHIFT is held,
a button press emits the shifted code. SHIFT itself emits
`EN_SHIFT (31)` press/release (the serial path also tracks it in
`shiftBtnState`, `input_events.cpp`).

| Panel event | Runtime packet | Notes |
|---|---|---|
| pad n ↓ (vel v) | PAD/0 `[n−1, 1, v, 0]` | state = BTN_PRESSED |
| pad n ↑ | PAD/0 `[n−1, 0, 0, 0]` | |
| button MAIN | BUTTON/0 `[8, state]` | shift → GRID `[9, …]` |
| button BROWSE | BUTTON/0 `[10, state]` | shift → SAVE `[11, …]` |
| button TRACK_MIX | `[12, …]` | shift → PAD_MIX `[13, …]` |
| button TRACK_MUTE | `[14, …]` | shift → PAD_MUTE `[15, …]` |
| button NEXT_SEQ | `[16, …]` | shift → FX `[17, …]` |
| button TAP_TEMPO | `[18, …]` | shift → MASTER `[19, …]` |
| button MENU | `[22, …]` | shift → PREF `[23, …]` |
| button NOTE_REPEAT | `[24, …]` | shift → LATCH `[25, …]` |
| button STEP_SEQ | `[32, …]` | shift → AUTOMATION `[33, …]` |
| button TC | `[34, …]` | shift → ON_OFF `[35, …]` |
| button SAMPLER | `[36, …]` | shift → LOOPER `[37, …]` |
| button SAMPLE_EDIT | `[38, …]` | shift → LINK_EDIT `[39, …]` |
| button PROG_EDIT | `[40, …]` | shift → MIDI_CONTROL `[41, …]` |
| button A/B/C/D | `[0..3, …]` | shift → BANK_E..H `[4..7, …]` |
| button SHIFT | `[31, state]` | also drives the translation above |
| button FULL_LEVEL | `[48, state]` | runtime also answers with its LED frame |
| button 16_LEVELS | `[50, state]` | id is `16_LEVELS` in the panel, `EN_16LEVELS` in contracts |
| button ERASE | `[52, state]` | |
| button REC / OVERDUB / STOP / PLAY / PLAY_START | `[42/43/44/45/46, state]` | |
| button CUR_RIGHT / CUR_LEFT | jog detent ±1 (below) | ⚠ no BtnLedSequence counterpart — see gaps |
| button CUR_UP / CUR_DOWN | BUTTON/0 `[21 EN_PLUS / 27 EN_MINUS, state]` | ⚠ approximation — see gaps |
| button INPUT | — | ⚠ **no runtime counterpart** (panel README: its function is a guess). Emits nothing; logged. |
| jog delta ±1 | BUTTON/0 `[30 EN_KNOB_CW / 28 EN_KNOB_CCW, 1]` per detent | the shape the screens actually switch on (`kick_AppGrid.cpp:513`, step sequencer) and the sim emits; a KNOBS/EN_KNOB_ROTARY center-knob shape also exists in contracts but is not what the screens key on |
| knob P1..P4 (absolute 0..1) | KNOBS/1 `[idx 0..3, dir, steps]` | runtime knobs are relative encoders; bridge keeps last value per knob, steps = round(Δ × 127) (sim qlink scale), one packet per change |
| knob MASTER / REC (absolute 0..1) | KNOBS/1 `[4 / 5, dir, steps]` | ⚠ **unverified codes** — no in-tree source assigns MASTER/REC knob codes (upstream's own sim leaves both unwired); qlink-shaped so the GUI's knob branch treats them uniformly. Flagged, may be inert. |
| strip value 0..1 | SLIDER/0 `[round(v × 100)]` | ⚠ no consumer of slider *input* found in kick_ui (only the contracts helper reads it); sent in the protocol's own shape, effect unverified — same status as upstream's sim, whose strip emits nothing at all |
| (bridge, on DCL connect) | SYSTEM/4 `[1]` | comm-init announce, mirroring the MCU; the GUI answers with a SYSTEM/2 frame down the wire — a free both-directions handshake check |

Runtime → web panel (feedback):

| Runtime packet | Panel call |
|---|---|
| LED/0 `[code, state, colour]` | `MPC.setLed(name(code), state != 0)` — name via reverse BtnLedSequence table (BANK_E..H fold to the A..D buttons + the panel's own bank display is *not* driven; see gaps) |
| LED/1 `[code, …]` blink | `setLed(name, true)` + bridge-side note (no blink primitive in MPC; approximated as on) |
| PAD/1 `[pad, state, colourId]` | `MPC.setPad(pad+1, colourFor(colourId), stateLevel(state))` — colourId → the contracts' PadColourIds palette, approximated to CSS colours; StateSampleAssigned/On → level 1, StateOff → 0, blink states → 0.6 |
| PAD/2 `[state, colourId]` (all pads one colour) | setPad for all 16 |
| PAD/5 colour+state bulk `[16 colours…, 16 states…]` | per-pad setPad |
| PAD/8 blink `[pad, state, onColour, offColour]` | setPad with onColour, level 0.6 (approximation) |
| SLIDER/1..3 `[percent]` | strip segment LEDs are not settable via MPC (no API) — logged only |
| SYSTEM/* | logged (incl. the COMM_INIT handshake reply and BPM) |
| anything else (MIDI/CV/unknown) | logged verbatim, not forwarded |

### Gaps, honestly

- **INPUT button**: no runtime counterpart exists. Panel-only.
- **CUR_UP/CUR_DOWN**: no BtnLedSequence codes for jog-ring cursor
  keys exist; mapped to EN_PLUS/EN_MINUS as the nearest navigation
  semantics. CUR_LEFT/CUR_RIGHT fold into jog detents for the same
  reason. All four flagged here rather than silently invented.
- **Jog push (EN_KNOB_ENTER, 26)**: the panel's jog emits deltas only
  — no click event exists in `window.MPC`, so KNOB_ENTER is
  unreachable from the web panel this pass.
- **MASTER/REC knob codes 4/5**: invented qlink indices (see table).
- **Banks E–H feedback**: LED frames for codes 4..7 light the A..D
  buttons (folded); the panel's own `MPC.bank` display state is
  panel-internal and not driven by the bridge.
- **Strip input** has no found consumer in the runtime UI; **strip
  LED output** has no MPC API. Both directions logged.
- **Pad colours** are approximations: the runtime speaks in
  PadColourIds palette indices; the panel speaks CSS colours.
- **The screen**: `MPC.takeScreen()` streaming of the GUI frame is
  the designed follow-on (see end), **not built this pass**.

## What was built (Oct 29, 2026)

Runtime source changes — exactly one file:

- `Kick/modules/3rdParty/juce_serialport/juce_serialport_Linux.cpp` —
  ElfKick's fail-safe stub replaced by a **real termios backend**
  mirroring the module's OSX implementation (same header/API):
  `/dev` enumeration (`ttymxc*`, `ttyUSB*`, `ttyACM*`), raw termios
  open with config (baud map incl. 230400, 8N1…), buffered async input
  stream with change notification, buffered output stream. One
  deliberate deviation from OSX: the input thread waits out a −1
  descriptor instead of exiting (port opens after stream creation on
  reconnect). `KickDeviceControl` rebuilt clean
  (`[100%] Built target KickDeviceControl`, 796 KB) and restaged into
  `build-app/system/`. Nothing else in the runtime changed;
  KT_KickEM is untouched.

New files:

- `panel/bridge.py` — the bridge (Python 3 stdlib only): pty owner
  (creates `/dev/ttymxc0` → pty slave symlink; EIO-tolerant reader,
  since a pty master reads EIO while DCL is down), PktMgr codec
  (CRC-verified streaming parser), the mapping/shift/knob-delta
  translation above, a hand-rolled RFC 6455 WebSocket server
  (127.0.0.1:8794), and an HTTP server for the panel directory
  (127.0.0.1:8793). All wire traffic is logged with timestamps.
- `panel/bridge.js` — page glue: forwards every `MPC` event as JSON;
  on connect sets `MPC.sound = false` and syncs knob baselines;
  applies `led` → `MPC.setLed`, `padlight` → `MPC.setPad`; shows a
  small "bridge: connected/disconnected" badge in the page header;
  auto-reconnects. Loaded via a `<script src="bridge.js">` tag added
  to `template.html`; `index.html` regenerated with `build.py`
  (generated-file workflow respected).
- `panel/test_client.py` — protocol-level test client speaking the
  exact JSON bridge.js speaks (`pad` / `browse` / `all` modes).
- `scripts/serve-panel.sh` — one command: starts the bridge if not
  running, and (unless `--no-restart-dcl`) restarts KickDeviceControl
  if it predates the pty, since a DCL that started without
  `/dev/ttymxc0` never retries on its own; KickMain respawns it and
  the new instance opens the port. Start the rig (RUNTIME_LOG recipe)
  and run this in either order; then open
  http://127.0.0.1:8793/index.html.

## Verification (Oct 29, 2026) — protocol level, full rig

Verified level, stated plainly: **the WebSocket protocol level** —
`test_client.py` and inline drivers sending byte-identical JSON to
what `bridge.js` sends, against the real bridge and the real staged
rig (KickMain set, no KT_KickEM in the loop). A click inside a real
browser page was **not** verified: the only browser binary on this VM
is the harness-gated Muse Browser build, which refuses to start
outside its harness ("Hatch WebGL domain policy is required for
browser startup"), and no other browser is installed. `bridge.js`
itself is therefore unrun in a browser here — it uses only the
documented `window.MPC` API and a standard WebSocket, but that claim
is by inspection, not by execution.

Evidence, in order:

1. **Wire handshake (both directions, unprompted):** with the bridge
   up first, DCL opened `/dev/ttymxc0`, and its connect-time
   `SYSTEM/2 data=[1]` (COMM_INIT/CONNECTED) frame arrived at the
   bridge; the bridge's comm-status announce was logged by DCL itself
   (`KickLogs`: `[KickDeviceControl] Data[0] : 1 Cmd : 4 type : 0` —
   parsed by the real `CPktMgr`). The GUI immediately began emitting
   feedback frames (SLIDER bar-LED percents, a PAD settings frame) —
   it treats the bridge as its control surface.
2. **Button in + LED out (TRACK_MIX):** WS button → GUI log
   `Data[0] From DCL:12 cmd 0 type 1 … register handle command`
   (KickUserInterface process), GUI switched to the Track Mixer
   screen, and the feedback LED frames came back over the wire as WS
   messages `{"led","MAIN",false}`, `{"led","TRACK_MIX",true}`.
3. **Pad → voice (the gate):** ElfTest01 loaded in the GUI (File
   Browser, frames `panel-bridge-project-loaded.png`); WS pad 1
   press → bridge tx frame `4d540300000500017f003e 0a` → GUI log
   `Data[0] From DCL:0 … type 3 … register handle command` → the Elf
   Drums program's **render-fed in-meters lit to ~80 %**
   (`panel-bridge-progmeter-lit.png`: L/R gradient bars where the
   baseline crop is black), with burst-frame AE in the meter row of
   4410 / 4662 / 1548 across successive presses — light, then decay,
   tracking kick.wav's envelope. Same evidence standard as
   PAD_GATE_LOG. (Single-shot timed captures raced the 0.4 s voice —
   AE 0 on the first attempt; the burst method is the reliable one.)
4. **BROWSE round trip (the scripted gate):** `test_client.py browse`
   → exit 0, **PASS** — bridge emitted
   `{"led","BROWSE",on:true}` (with TRACK_MIX off), GUI logged code
   10 delivered, and the GUI screen switched from Track Mixer to the
   File Browser (`panel-bridge-filebrowser.png` class frame).
   Cosmetic artifact, disclosed: the test client's naive handshake
   reader can swallow the bridge's `{"t":"ready"}` greeting (bytes
   after the HTTP headers); it does not affect the page's real
   WebSocket stack or any test verdict.

The rig and bridge were left running in the passing configuration.

## Follow-ons (designed, not built)

- **Screen streaming into `MPC.screen`:** the GUI renders KickHome at
  1024×600 on its X display; a follow-on can grab that framebuffer
  (X11 capture in the bridge host, or a tiny streamer in the GUI
  process), scale to the canvas's 1184×680, and push frames over the
  same WebSocket (binary frames), with `MPC.takeScreen()` called once
  by bridge.js so the panel's built-in display yields. Bandwidth at
  10–15 fps MJPEG-style is trivial on localhost. Not built this pass,
  per scope.
- **Browser-level click verification** once a runnable browser
  exists on the VM (any Chromium/Firefox): load the served page,
  confirm the badge + "Sound: runtime", click pad 1 and BROWSE with
  xdotool, and re-check the same logs + the page-side BROWSE `lit`
  class. All server-side pieces are proven; this closes the last
  client-side unknown.
- **Pad colour fidelity:** PadColourIds → CSS is an approximation
  table in `bridge.py`; the sim's `getPadColourFromId` palette
  (kick_Simulator.cpp) is the in-tree reference if exact hues matter.
- **KNOB_ENTER (jog push)** needs a panel event that doesn't exist;
  if the panel gains one, the bridge mapping is one line
  (BUTTON/0 `[26, state]`).
