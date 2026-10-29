#!/usr/bin/env python3
"""Generate the groovebox front panel.

Writes:
  panel.svg   static, standalone vector of the panel (every control has an id)
  index.html  playable panel: template.html with the live SVG inlined

Coordinates are in a 960x960 panel space traced from the reference photo.
"""
from pathlib import Path

HERE = Path(__file__).parent

FONT = "'Arial Narrow','Helvetica Neue',Arial,sans-serif"

# Pad ring colours, pad 1..16 (1 = bottom-left, 16 = top-right), from the photo
PAD_COLORS = {
    13: "#ff5a3c", 14: "#ffb02e", 15: "#8fd3ff", 16: "#ff8a2a",
    9: "#f2f2f2", 10: "#ff3d6e", 11: "#ff3b3b", 12: "#7fc8ff",
    5: "#b34dff", 6: "#ff3dd2", 7: "#ff5a3c", 8: "#7fd8ff",
    1: "#ff4fa8", 2: "#ff7a2e", 3: "#f2f2f2", 4: "#ffd23f",
}

CYAN, PINK, RED, WHITE, GREEN, BLUE, PURPLE = (
    "#4fd8ff", "#ff5fc8", "#ff4a4a", "#f4f4f4", "#5dff9a", "#5a8cff", "#c06bff")

# id, x, y, label (\n = two lines), sub label, colour
BUTTONS = [
    ("A", 644, 496, "A", "E", CYAN), ("B", 730, 496, "B", "F", RED),
    ("C", 820, 496, "C", "G", CYAN), ("D", 910, 496, "D", "H", RED),
    ("MAIN", 644, 580, "MAIN", "GRID", CYAN),
    ("BROWSE", 711, 580, "BROWSE", "SAVE", PINK),
    ("TRACK_MIX", 778, 580, "TRACK\nMIX", "PAD MIX", WHITE),
    ("TRACK_MUTE", 845, 580, "TRACK\nMUTE", "PAD MUTE", CYAN),
    ("NEXT_SEQ", 912, 580, "NEXT\nSEQ", "FX", CYAN),
    ("TAP_TEMPO", 644, 664, "TAP\nTEMPO", "MASTER", PINK),
    ("MENU", 912, 664, "MENU", "PREFS", GREEN),
    ("NOTE_REPEAT", 644, 744, "NOTE\nREPEAT", "LATCH", CYAN),
    ("SHIFT", 912, 744, "SHIFT", "", GREEN),
    ("STEP_SEQ", 644, 824, "STEP\nSEQ", "AUTO", PINK),
    ("TC", 711, 824, "TC", "ON/OFF", CYAN),
    ("SAMPLER", 778, 824, "SAMPLER", "LOOP", BLUE),
    ("SAMPLE_EDIT", 845, 824, "SAMPLE\nEDIT", "LINK EDIT", PINK),
    ("PROG_EDIT", 912, 824, "PROG\nEDIT", "MIDI CTRL", GREEN),
    ("FULL_LEVEL", 70, 752, "FULL\nLEVEL", "HALF LVL", PINK),
    ("16_LEVELS", 70, 836, "16\nLEVELS", "NOTES", PINK),
    ("ERASE", 70, 912, "ERASE", "", PINK),
]


def text(x, y, s, size=11, fill="#a8a8a8", anchor="middle", weight=700, extra=""):
    return (f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" '
            f'text-anchor="{anchor}" font-weight="{weight}" {extra}>{s}</text>')


def button(bid, x, y, label, sub, color, w=56, h=42):
    lines = label.split("\n")
    lh = 11
    y0 = y - (len(lines) - 1) * lh / 2 + 4
    t = "".join(text(x, y0 + i * lh, ln, 10.5, color) for i, ln in enumerate(lines))
    shift_ring = (f'<rect x="{-w/2-3}" y="{-h/2-3}" width="{w+6}" height="{h+6}" rx="8" '
                  f'fill="none" stroke="{color}" stroke-width="1.5" opacity=".6"/>'
                  if bid == "SHIFT" else "")
    out = (f'<g id="btn-{bid}" class="btn" data-ctl="button" data-id="{bid}" style="--c:{color}">'
           f'<g transform="translate({x},{y})">{shift_ring}'
           f'<rect class="cap" x="{-w/2}" y="{-h/2}" width="{w}" height="{h}" rx="6" '
           f'fill="url(#btnGrad)" stroke="#000" stroke-width="1.5"/></g>'
           f'<g class="led">{t}</g></g>')
    if sub:
        out += text(x, y + h / 2 + 15, sub, 10.5)
    return out


def knob(kid, x, y, r, label=None, minmax=False):
    out = ""
    if label:
        out += text(x, y - r - 12, label, 12, "#d0d0d0")
    if minmax:
        out += text(x - r + 4, y + r + 18, "MIN", 9) + text(x + r - 2, y + r + 18, "MAX", 9)
        out += f'<circle cx="{x - r + 2}" cy="{y + r + 4}" r="1.6" fill="#888"/>'
        out += f'<circle cx="{x + r - 2}" cy="{y + r + 4}" r="1.6" fill="#888"/>'
    ridges = "".join(
        f'<line x1="0" y1="{-r}" x2="0" y2="{-r+5}" stroke="#000" stroke-width="2" '
        f'transform="rotate({a})"/>' for a in range(0, 360, 20))
    out += (f'<g id="knob-{kid}" class="knob" data-ctl="knob" data-id="{kid}" '
            f'transform="translate({x},{y})">'
            f'<circle r="{r+5}" fill="#0b0b0c"/>'
            f'<circle r="{r}" fill="url(#knobGrad)" stroke="#000" stroke-width="1.5"/>'
            f'<g class="rot" transform="rotate(0)">{ridges}'
            f'<circle r="{r*0.68}" fill="url(#knobTop)"/>'
            f'<line x1="0" y1="{-r*0.25}" x2="0" y2="{-r*0.86}" stroke="#f2f2f2" '
            f'stroke-width="3" stroke-linecap="round"/></g></g>')
    return out


def pads():
    out = ""
    x0, y0, step, w, h = 136, 480, 114, 100, 96
    for row in range(4):
        for col in range(4):
            n = (3 - row) * 4 + col + 1
            c = PAD_COLORS[n]
            x, y = x0 + col * step, y0 + row * step
            out += (f'<g id="pad-{n}" class="pad" data-ctl="pad" data-id="{n}" '
                    f'transform="translate({x},{y})" style="--c:{c}">'
                    f'<rect class="glow" x="-2" y="-2" width="{w+4}" height="{h+4}" rx="8" '
                    f'fill="{c}" opacity="0" filter="url(#glow)"/>'
                    f'<rect class="base" width="{w}" height="{h}" rx="6" fill="url(#padGrad)" '
                    f'stroke="#000" stroke-width="2"/>'
                    f'<rect class="ring" x="2" y="2" width="{w-4}" height="{h-4}" rx="5" '
                    f'fill="none" stroke="{c}" stroke-width="2.5" opacity=".55"/>'
                    f'<rect class="lit" x="3" y="3" width="{w-6}" height="{h-6}" rx="4" '
                    f'fill="{c}" opacity="0"/></g>')
            out += text(x + 3, y + h + 13, n, 9, "#9a9a9a", "start")
    return out


def transport():
    icons = {
        "REC": '<circle r="6.5" fill="#ff4040"/>',
        "OVERDUB": '<circle r="7" fill="none" stroke="#f4f4f4" stroke-width="2"/>'
                   '<circle r="2.8" fill="#f4f4f4"/>',
        "STOP": '<rect x="-6" y="-6" width="12" height="12" rx="1.5" fill="#f4f4f4"/>',
        "PLAY": '<path d="M-5 -7 L8 0 L-5 7 Z" fill="#5dff9a"/>',
        "PLAY_START": '<rect x="-8" y="-7" width="3" height="14" fill="#5dff9a"/>'
                      '<path d="M-2 -7 L9 0 L-2 7 Z" fill="#5dff9a"/>',
    }
    out = ""
    for i, (bid, icon) in enumerate(icons.items()):
        x, y = 644 + i * 67, 904
        out += (f'<g id="btn-{bid}" class="btn round" data-ctl="button" data-id="{bid}">'
                f'<g transform="translate({x},{y})">'
                f'<circle class="cap" r="21" fill="url(#btnGrad)" stroke="#000" stroke-width="1.5"/>'
                f'<g class="led">{icon}</g></g></g>')
    return out


def jog():
    cx, cy = 778, 708
    out = f'<g transform="translate({cx},{cy})">'
    out += '<circle r="86" fill="#0a0a0b" stroke="#2c2c2e" stroke-width="2"/>'
    # cursor ring: four quadrant buttons around the wheel
    for bid, rot in (("CUR_UP", 0), ("CUR_RIGHT", 90), ("CUR_DOWN", 180), ("CUR_LEFT", 270)):
        out += (f'<g id="btn-{bid}" class="btn cursor" data-ctl="button" data-id="{bid}" '
                f'transform="rotate({rot})">'
                f'<path class="cap" d="M -52.3 -60.1 A 80 80 0 0 1 52.3 -60.1 L 34.4 -40.3 '
                f'A 53 53 0 0 0 -34.4 -40.3 Z" fill="url(#btnGrad)" stroke="#000" stroke-width="1.5"/>'
                f'<g class="led"><path d="M -6 -63 L 0 -70 L 6 -63" fill="none" stroke="#bbb" '
                f'stroke-width="2.5" stroke-linecap="round"/></g></g>')
    out += '<circle r="53" fill="#050505"/>'
    out += ('<circle id="jog-ring" r="50" fill="none" stroke="#ff2a2a" stroke-width="3" '
            'opacity=".85" filter="url(#glow)"/>')
    out += ('<g id="jog" class="jog" data-ctl="jog" data-id="JOG">'
            '<circle r="46" fill="url(#knobGrad)" stroke="#000" stroke-width="2"/>'
            '<g class="rot">'
            + "".join(f'<line x1="0" y1="-46" x2="0" y2="-40" stroke="#000" stroke-width="2" '
                      f'transform="rotate({a})"/>' for a in range(0, 360, 12))
            + '<circle r="30" fill="url(#knobTop)"/>'
              '<circle cx="0" cy="-20" r="5" fill="#1a1a1a" stroke="#000"/></g></g>')
    out += '</g>'
    return out


def strip():
    out = ('<rect x="30" y="484" width="80" height="206" rx="6" fill="#121213" '
           'stroke="#2c2c2e" stroke-width="2"/>')
    out += ('<g id="strip" class="strip" data-ctl="strip" data-id="STRIP">'
            '<rect class="track" x="48" y="498" width="44" height="178" rx="4" fill="#070707" '
            'stroke="#000" stroke-width="1.5"/>')
    for i in range(16):
        y = 666 - i * 10.6
        out += (f'<rect class="seg" data-i="{i}" x="78" y="{y:.1f}" width="8" height="5" rx="1" '
                f'fill="#ff2020" opacity=".12"/>')
    out += '</g>'
    return out


def screen(live):
    out = ('<rect x="160" y="28" width="640" height="396" rx="10" fill="#050505" '
           'stroke="#2c2c2e" stroke-width="2"/>')
    if live:
        out += ('<foreignObject x="184" y="58" width="592" height="340">'
                '<canvas xmlns="http://www.w3.org/1999/xhtml" id="screen" width="1184" '
                'height="680" style="width:592px;height:340px;display:block;border-radius:4px">'
                '</canvas></foreignObject>')
    else:
        out += ('<rect id="screen" x="184" y="58" width="592" height="340" rx="4" '
                'fill="url(#screenGrad)"/>')
    return out


def panel(live):
    defs = f'''<defs>
<linearGradient id="bodyGrad" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#2b2b2e"/><stop offset="1" stop-color="#1b1b1d"/></linearGradient>
<linearGradient id="btnGrad" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#303033"/><stop offset="1" stop-color="#18181a"/></linearGradient>
<radialGradient id="padGrad" cx=".5" cy=".4" r=".75">
 <stop offset="0" stop-color="#2a2a2c"/><stop offset="1" stop-color="#151516"/></radialGradient>
<radialGradient id="knobGrad" cx=".4" cy=".35" r=".8">
 <stop offset="0" stop-color="#4a4a4d"/><stop offset="1" stop-color="#0e0e0f"/></radialGradient>
<radialGradient id="knobTop" cx=".4" cy=".3" r=".9">
 <stop offset="0" stop-color="#3a3a3d"/><stop offset="1" stop-color="#161617"/></radialGradient>
<linearGradient id="screenGrad" x1="0" y1="0" x2="1" y2="1">
 <stop offset="0" stop-color="#1c1c1e"/><stop offset="1" stop-color="#0c0c0d"/></linearGradient>
<filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
 <feGaussianBlur stdDeviation="5" result="b"/>
 <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<filter id="softglow" x="-50%" y="-50%" width="200%" height="200%">
 <feGaussianBlur stdDeviation="2.2" result="b"/>
 <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
</defs>'''
    body = [
        '<rect width="960" height="960" rx="18" fill="url(#bodyGrad)" stroke="#000" stroke-width="3"/>',
        '<rect x="3" y="3" width="954" height="954" rx="16" fill="none" stroke="#3a3a3d" stroke-width="1"/>',
        # power / status LED window
        '<rect x="46" y="34" width="62" height="32" rx="4" fill="#0c0c0d" stroke="#2c2c2e"/>',
        '<circle id="led-power" cx="62" cy="50" r="5" fill="#d8ffd8" filter="url(#softglow)"/>',
        '<circle cx="90" cy="50" r="8" fill="#050505" stroke="#333"/>',
        screen(live),
        knob("MASTER", 70, 150, 24, "MASTER", True),
        knob("REC", 70, 270, 24, "REC", True),
        # input selector with level LEDs
        '<rect x="40" y="344" width="80" height="86" rx="6" fill="#121213" stroke="#2c2c2e" stroke-width="2"/>',
        "".join(f'<rect class="in-led" data-i="{i}" x="{54 + i*14}" y="356" width="10" height="4" '
                f'rx="1" fill="#ff3030" opacity="{1 if i == 0 else .15}"/>' for i in range(4)),
        button("INPUT", 80, 396, "", "", "#888", 50, 34),
        strip(),
        pads(),
        '<line x1="618" y1="462" x2="940" y2="462" stroke="#555" stroke-width="1"/>',
        f'<rect x="752" y="452" width="52" height="18" fill="url(#bodyGrad)"/>',
        text(778, 466, "BANK", 11, "#c8c8c8"),
        "".join(button(*b) for b in BUTTONS),
        jog(),
        transport(),
        "".join(knob(f"P{i+1}", 884, 80 + i * 94, 22) + text(884, 80 + i * 94 + 42, f"P{i+1}", 11, "#c8c8c8")
                for i in range(4)),
    ]
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-12 -12 984 984" '
            f'font-family="{FONT}" role="img" aria-label="Groovebox front panel">'
            + defs + "".join(body) + '</svg>')


if __name__ == "__main__":
    (HERE / "panel.svg").write_text(panel(live=False))
    tpl = (HERE / "template.html").read_text()
    (HERE / "index.html").write_text(tpl.replace("<!--PANEL-->", panel(live=True)))
    print("wrote panel.svg and index.html")
