# Groovebox Panel

A playable vector model of an unreleased groovebox's front panel (traced from a press photo), meant as the front end for a software emulator. Use it with a mouse, a computer keyboard or a MIDI controller.

## Files

| File | What it is |
|---|---|
| `panel.svg` | Static vector of the panel. Every control has an id: `pad-1`…`pad-16` (1 = bottom-left), `knob-MASTER`, `knob-REC`, `knob-P1`…`knob-P4`, `btn-<NAME>`, `jog`, `strip`, `screen`. |
| `index.html` | The playable panel (generated, do not edit by hand). |
| `template.html` | Page source: styles, input handling, drum kit, MIDI, display, `window.MPC` API. |
| `build.py` | Draws the panel. Run `python3 build.py` after editing it or `template.html`; it rewrites `panel.svg` and `index.html`. |

## Run

Open `index.html` in Chrome or Edge (Safari has no Web MIDI). If MIDI is blocked from a `file://` page, serve it locally:

```bash
python3 -m http.server 8793
```

then open http://localhost:8793/index.html.

## Controls

- Pads: click (higher on the pad = louder) or keys `1-4` / `Q-R` / `A-F` / `Z-V` (top row to bottom row).
- Knobs: drag vertically or scroll; Shift = fine; double-click = centre.
- Jog wheel: drag in a circle, scroll, or `[` / `]`. The four ring segments are cursor buttons (arrow keys).
- Touch strip: drag.
- Transport: Space = PLAY, Esc = STOP, Enter = REC.
- Built-in kit: P1 tune, P2 decay, P3 filter, P4 delay, MASTER volume. Drop an audio file on a pad to load it.
- Working button logic: banks A–D (Shift = E–H), FULL LEVEL, TAP TEMPO, NOTE REPEAT (hold, or Shift to latch), transport LEDs, INPUT selector LEDs. All other buttons only emit events.

## MIDI

Default map: notes 36–51 → pads 1–16; CC 70–75 → P1–P4, MASTER, REC; CC1 and pitch bend → touch strip.
Click **MIDI learn**, click a control, then move a control on the controller to remap. Mappings are stored in the browser's localStorage (`mpc-midi-map`).

## Emulator API (`window.MPC`)

```js
MPC.on('pad',    ({pad, velocity, down}) => {})  // pad 1..16, velocity 0..127
MPC.on('button', ({id, down}) => {})             // 'PLAY', 'SHIFT', 'A', 'CUR_UP', ...
MPC.on('knob',   ({id, value}) => {})            // 'MASTER','REC','P1'..'P4', 0..1
MPC.on('jog',    ({delta}) => {})                // +1 / -1 per detent
MPC.on('strip',  ({value}) => {})                // 0..1
MPC.on('aftertouch', ({note, value}) => {})      // MIDI poly aftertouch passthrough
MPC.on('*', (type, detail) => {})

MPC.setLed(id, on)            // button LED
MPC.setPad(n, color, level)   // pad light, level 0..1, color null = pad's own
MPC.setKnob(id, value)        // move a knob without emitting
MPC.screen                    // <canvas> 1184x680 behind the display
MPC.takeScreen()              // stop the built-in display, then draw your own
MPC.sound = false             // silence the built-in kit
```

## Known gaps

- Not tested with a physical MIDI controller (only simulated messages).
- No MIDI out / LED feedback to the controller yet.
- The INPUT button under the REC knob is a guess at that control's function.
- Brand logo intentionally left off.
