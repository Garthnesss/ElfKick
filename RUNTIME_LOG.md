# Runtime investigation log — ElfKick

Date: Oct 29, 2026 · VM: Ubuntu, gcc 13, 2 cores, root available
Problem: `KickUserInterface` launches headless (Xvfb), stays alive
(101 threads, IPC + timers active), maps a fullscreen window — but the
window paints **uniformly black**, even with KickAudioModel +
KickAudioEngine + KickDeviceControl running as peers.
Evidence frame: `kick-gui-screenshot.png` (black, from Phase 3).

## Code facts established before testing

- GUI entry (`Kick/apps/kick_gui/kick_gui_main.cpp`): window content is
  `kick_ui::KickHome::Instance()` in a `DocumentWindow` whose background
  colour is literally `juce::Colours::black`. KickHome itself has **no
  paint()** — everything visible must come from its child screens
  (first: `ui::RecentProject`), the top bar and the status bar.
- KickHome ctor emits `sigScreenOpen(Screen::RecentProject)` → async
  `load()` → `setupLoadScreenVisibility()` makes that screen visible.
  No IPC handshake gates this path in code.
- **All UI imagery and colours come from files, not embedded data:**
  `ui::Theme::loadTheme("Default")` reads
  `getAppDirs(MTAssets)/Default/theme.xml`; on Linux
  `MTAssets = /app/assets` (`common/source/kick_paths.cpp`).
  If that file is missing, the colour palette stays empty and
  `Theme::getColour()` falls through to its worst case:
  **`juce::Colours::transparentBlack` for every colour** — backgrounds,
  text, outlines. Every image (`Theme::getImage` →
  `ImageCache::getFromFile`) is likewise null. A fully alive UI would
  therefore paint as a uniform black rectangle over the black window.
- Linux content paths (kick_paths.cpp): assets `/app/assets`,
  samples `/data/sounds`, demos `/app/demos/`, templates
  `/app/templates/`, projects `/home/Kick/projects/`, logs
  `/home/Kick/logs/`, prefs `/home/Kick/prefs`.
- VM state at investigation start: **`/app` does not exist at all**;
  `/home/Kick` contains only `prefs` (created by the GUI's own
  settings init on earlier runs). The repo ships the full factory
  asset tree at `Kick/assets/Default/` including `theme.xml`.
- `RecentProject::addDemos()/addTemplates()` scan `/app/demos` and
  `/app/templates` for subdirectories; each becomes a tile.
  `RecentItems::setProperties()`: a dir containing a `*.png` shows that
  image (resolved via the theme assets `recent_project/<name>`);
  a dir without one shows its name + timestamps as text labels.

## Hypotheses (in test order)

1. **H1 — missing factory content (esp. /app/assets theme)** makes every
   colour transparent and every image null → black window.
   Test: install repo `Kick/assets` → `/app/assets`, create
   `/app/demos/Demo 1` (+ a `Demo 01.png` inside, mirroring the asset
   tile names), `/app/templates/Template 1`, `/home/Kick/projects`,
   `/home/Kick/logs`, `/data/sounds`; relaunch GUI with peers; capture.
2. **H2 — supervisor orchestration**: KickMain launches/configures the
   processes in an order/with args the GUI needs. Test: read
   kick_main.cpp + init scripts; launch the way KickMain would, or via
   the combined `Kick` app.
3. **H3 — GUI waits on a state/message** (project load, IPC "ready"):
   instrument via logs/stdout/gdb if H1+H2 fail.

## Tests

### T1 — H1 full factory layout (GUI + peers launched manually) → **PAINTS**

Installed (all paths from kick_paths.cpp):

```sh
mkdir -p "/app/demos/Demo 1" "/app/templates/Template 1" \
         /home/Kick/projects /home/Kick/logs /data/sounds
cp -r Kick/assets /app/assets            # ⇒ /app/assets/Default/theme.xml + images
cp "/app/assets/Default/recent_project/Demo 01.png"     "/app/demos/Demo 1/"
cp "/app/assets/Default/recent_project/Template 01.png" "/app/templates/Template 1/"
```

Launched KickAudioModel, KickAudioEngine, KickDeviceControl, then
KickUserInterface under one Xvfb (`ALSA_CONFIG_PATH=$PWD/asound-null.conf`,
DISPLAY=:99, 1920x1200). All four stayed alive; GUI window capture:
**37,531 colours** (black frame had 2). The frame shows the real
RecentProject home screen: header (K logo, "Projects", file-browser
button, bank "A", status icons), the "Demo 01" tile with artwork and
selection border, the "Template 01" tile, and the new-project tile.
Saved as `kick-gui-screenshot.png` (overwrites the black frame).

### T2 — Bisect: assets only, demos/templates emptied → **still paints**

GUI alone, `/app/assets` present, `/app/demos` + `/app/templates`
empty: capture = **537 colours** — header, title, icons and the
new-project tile all render; only the demo/template tiles are gone
(the RecentProject screen's own background is near-black by design).
**Conclusion: `/app/assets` (the theme) is the necessary and
sufficient piece. Demos/templates only populate tiles.**

### Root cause (confirmed)

There is no code bug and nothing the GUI was "waiting" for. The UI
draws **exclusively** from the factory asset tree: `Theme::loadTheme`
parses `/app/assets/Default/theme.xml` for every colour, and every
image is file-loaded from the same tree. With `/app` absent (the VM's
state; no build/install step in this repo ever populates it), the
palette is empty and `Theme::getColour()` returns
`transparentBlack` for *everything*, while all images are null — so a
fully initialised UI paints transparent-on-black over the
DocumentWindow's black background: a uniform black frame. On hardware
the Buildroot rootfs ships `/app/assets`; off-hardware it must be
installed by hand from the repo's own `Kick/assets/`.

### T3 — H2 supervisor route: KickMain with co-located binaries → **full system up**

KickMain's design (kick_main.cpp + contracts/config): it is an
`IPCApplication`; its children are described by `KickIPCConfig`
aggregating Model/Engine/Dcl/GUI configs, each child =
`BASE_PATH + <ExeName>`, where **BASE_PATH is the directory of the
running executable** (`ipc_config_constants.h`). Each child receives
its full JSON connection config as a single command-line argument
(visible verbatim in `ps`). On hardware, systemd's `ktkick.service`
runs `/usr/bin/kt_kick.sh`, which execs `/usr/bin/KT_Kick`.

Staged all five binaries into one dir (`build-app/system/`) and ran
`./KickMain` under Xvfb with the T1 content layout:

- Log: all four children "Started...", then "KickDeviceControl /
  KickAudioEngine / KickAudioModel / KickUserInterface to Kick Main
  is Connected" — **no exits** over the 20 s observation (contrast
  Phase 3, where the same supervisor restart-looped children that
  exited with code 255 within a second from their separate artefact
  dirs; co-location + a display for the group is the difference).
- GUI window captured from this run: 37,531 colours, identical home
  screen to T1.
- Side test, KickMain with **no DISPLAY**: engine/model/DCL children
  survive (they never open a window), but the GUI child exits
  immediately (code 0) and KickMain restart-loops it — the GUI is the
  only strictly display-bound process.

### T4 — Phase 3 gate: simulator + full system co-resident → **pass**

Ran the KickMain-supervised set on Xvfb :99 and `KT_KickEM`
(build-kick/) on Xvfb :100 simultaneously, content layout installed:

- All six processes alive at once; KickMain's log shows the full
  mesh connected (GUI↔Main, GUI↔Model, GUI↔DeviceControl,
  Model↔Engine, everyone↔Main).
- GUI capture: 37,531 colours (home screen). Simulator capture:
  33,590 colours — full hardware panel (pads, touch strip, MASTER/REC
  + P1–P4 knobs, banks, function buttons) **with the themed KickHome
  "Projects" screen rendered in its centre cut-out**, demo/template
  tiles included (saved as `kick-simulator-screenshot.png`).
- Capture caveat: on a *single* shared Xvfb the two fullscreen
  windows occlude each other and the lower one captures black —
  that's an X stacking artefact, not a paint failure (hence the
  two-server setup above).

## Exact launch recipe (reproduces the working desktop system)

```sh
# 1. Factory content (once)
mkdir -p "/app/demos/Demo 1" "/app/templates/Template 1" \
         /home/Kick/projects /home/Kick/logs /data/sounds
cp -r Kick/assets /app/assets
cp "/app/assets/Default/recent_project/Demo 01.png"     "/app/demos/Demo 1/"
cp "/app/assets/Default/recent_project/Template 01.png" "/app/templates/Template 1/"

# 2. Stage the processes in ONE directory (KickMain spawns siblings)
SYS=build-app/system; mkdir -p $SYS
cp build-app/apps/kick_main/KickMain_artefacts/Release/KickMain \
   build-app/apps/kick_gui/KickUserInterface_artefacts/Release/KickUserInterface \
   build-app/apps/kick_audio_engine/KickAudioEngine_artefacts/Release/KickAudioEngine \
   build-app/apps/kick_audio_model/KickAudioModel_artefacts/Release/KickAudioModel \
   build-app/apps/kick_device_control/KickDeviceControl_artefacts/Release/KickDeviceControl $SYS/

# 3. Run (headless)
export ALSA_CONFIG_PATH=$PWD/asound-null.conf
Xvfb :99 -screen 0 1920x1200x24 &
DISPLAY=:99 $SYS/KickMain &          # children + GUI home screen follow
DISPLAY=:99 build-kick/apps/kickem/KT_KickEM_artefacts/Release/KT_KickEM &   # optional hw peer
```

Notes: the UI is designed for a 1024x600 touchscreen, so at
1920x1200 the home screen's lists hug the left column — expected,
not a scaling bug. No source changes were needed for any of this;
the only tree additions are this log and the two screenshots.

# Addendum (Oct 30, 2026) — hard-won rig lessons from the synth build

**Bridge/DCL start order matters (TIOCEXCL).** The serial backend
sets TIOCEXCL on the pty slave; the flag persists while the bridge
holds the master, so after a KickDeviceControl dies, the NEXT DCL
silently fails to open /dev/ttymxc0 ("Serial Not Found") and the
pad/button path is dead with no GUI-visible error. Tell-tale: the
bridge log shows no `pty rx` (SYSTEM/SLIDER) frames from the new
DCL. Remedy: start the bridge FIRST (fresh pty), then the rig (or
restart just the DCL via `scripts/serve-panel.sh`, which does the
ordering for a running rig). KickMain-spawned DCLs are fine as long
as the pty they open is fresh.

**GUI meters cannot judge sustained sound.** The track meter
decays visually on constant input (and can even jump after a
release). It is valid for attack/decay signatures (drums) only.
For held-note truth, capture the engine's output: use
`asound-capture.conf` (file plugin direct — WARNING: unpaced, the
engine free-runs and writes ~15 MB/s) or, better,
`asound-fifo.conf` + `scripts/rig_capture_reader.py <bytes/s>`
(FIFO backpressure paces the engine; the negotiated format here
was float32, 4 channels, 48 kHz). Start the reader before the
engine. Evidence from the synth proof: `rig-captures/`.

**Sequence playback is silent in this rig.** The transport
counter advances, but no program (drum or synth) receives pattern
notes audibly — live pad input is the working note path. Also:
never switch projects while the transport plays (the stock app
refuses once, and once quit the GUI with exit code 0).
