#!/usr/bin/env python3
"""Page <-> map selftest for the ElfKick web panel (VIRTUAL_MPC_STUDY lift #1).

Cross-checks panel/controls.json — the single source of truth for the
control map — against the three sources it describes, in BOTH directions:

  page      index.html (built page) + template.html (emitters/handlers)
  glue      bridge.js  (which MPC events become which WS messages)
  bridge    bridge.py  (WS dispatch, BUTTON_CODES / KNOB_CODES /
            LED_CODE_TO_PANEL tables, the send_hw call shapes)
  contract  ../Kick/modules/contracts/dcl/dcl_contracts.h (the numeric
            codes' names — a manifest code that disagrees with the
            contracts enum fails here, not on the rig)

A control is allowed to have no wire frame ONLY if controls.json carries
a "gap" entry for it explaining why. Anything else dead or unmapped —
a drawn control with no handler, a handler with no dispatch, a dispatch
code no control claims — fails loudly with a named list.

Run:  python3 test_controls.py     (exit 0 = map is consistent)
"""

import ast
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent

FAILURES = []
CHECKS = 0


def check(name, bad):
    """bad: list of human-readable problems (empty = pass)."""
    global CHECKS
    CHECKS += 1
    if bad:
        FAILURES.append((name, bad))
        print(f"FAIL {name}")
        for b in bad:
            print(f"       - {b}")
    else:
        print(f"PASS {name}")


def load(name):
    return (HERE / name).read_text()


# ---------------------------------------------------------------- sources
manifest = json.loads(load("controls.json"))
controls = manifest["controls"]
seen, dup = set(), []
for c in controls:
    if c["id"] in seen:
        dup.append(c["id"])
    seen.add(c["id"])

template = load("template.html")
index = load("index.html")
bridge_js = load("bridge.js")
bridge_src = load("bridge.py")
hdr = (ROOT / "Kick/modules/contracts/dcl/dcl_contracts.h").read_text()

tree = ast.parse(bridge_src)


def bridge_table(name):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise KeyError(name)


BUTTON_CODES = bridge_table("BUTTON_CODES")
KNOB_CODES = bridge_table("KNOB_CODES")
LED_CODE_TO_PANEL = {int(k): v for k, v in bridge_table("LED_CODE_TO_PANEL").items()}
consts = {}
for node in tree.body:
    if isinstance(node, ast.Assign) and isinstance(node.value, ast.Tuple) \
            and isinstance(node.targets[0], ast.Tuple):
        for t, e in zip(node.targets[0].elts, node.value.elts):
            if isinstance(e, ast.Constant):
                consts[t.id] = e.value


def contracts_enum(name):
    body = re.search(r"enum\s+" + name + r"\s*\{(.*?)\};", hdr, re.S).group(1)
    out, val = {}, -1
    for line in body.splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            k, v = [s.strip() for s in line.split("=", 1)]
            val = int(v)
        else:
            k, val = line, val + 1
        out[k] = val
    return out


BTN = contracts_enum("BtnLedSequence")
BTN_BY_CODE = {v: k for k, v in BTN.items()}

# ------------------------------------------------------- 1. manifest sanity
bad = list(dup)
for c in controls:
    for field in ("id", "kind", "page_id", "page"):
        if field not in c:
            bad.append(f"{c.get('id', '?')}: missing field {field}")
    page = c.get("page", {})
    for field in ("element", "handler"):
        if not page.get(field):
            bad.append(f"{c['id']}: page.{field} missing")
    if c.get("frame") is None and "gap" not in c:
        bad.append(f"{c['id']}: no frame and no gap entry — a dead control "
                   f"must be declared, not discovered by clicking it")
check("manifest schema: unique ids, required fields, every frameless "
      "control carries a declared gap", bad)

# ------------------------------------------------- 2. page controls == map
page_ctls = set(re.findall(r'data-ctl="(\w+)" data-id="([\w]+)"', index))
manifest_ctls = {(c["page"]["data_ctl"], c["page_id"]) for c in controls
                 if c["page"].get("data_ctl")}
bad = [f"on the page but NOT in controls.json: {t} {i}"
       for t, i in sorted(page_ctls - manifest_ctls)]
bad += [f"in controls.json but NOT on the page: {t} {i}"
        for t, i in sorted(manifest_ctls - page_ctls)]
check("page <-> manifest: every drawn control (data-ctl) is mapped, "
      "every mapped control is drawn", bad)

# ------------------------------------------- 3. page handlers really exist
bad = []
for c in controls:
    el = c["page"]["element"]
    if c["page"].get("data_ctl"):
        if f'data-id="{c["page_id"]}"' not in index:
            bad.append(f"{c['id']}: element {el} not in the built page")
    elif el.lstrip("#") not in template or f'id="{el.lstrip("#")}"' not in template:
        bad.append(f"{c['id']}: page-chrome element {el} not in template.html")
for fn in ("padDown", "padUp", "buttonDown", "buttonUp", "knobSet",
           "jogStep", "jogPush", "stripSet"):
    if not re.search(rf"function {fn}\b", template):
        bad.append(f"template.html: handler funnel {fn}() is gone")
emitted = set(re.findall(r"MPC\.emit\('(\w+)'", template))
want_events = {c["page"]["event"] for c in controls if c["page"].get("event")}
declared_unbridged = {e["event"] for e in manifest.get("page_events_not_bridged", [])}
bad += [f"manifest claims page event '{e}' but template.html never emits it"
        for e in sorted(want_events - emitted)]
bad += [f"template.html emits MPC event '{e}' claimed by no control and "
        f"not declared in page_events_not_bridged"
        for e in sorted(emitted - want_events - declared_unbridged)]
check("page handlers: funnels exist, manifest events == emitted events", bad)

# ------------------------------------------------- 4. bridge.js forwarding
fwd = dict(re.findall(r"MPC\.on\('(\w+)',\s*d => send\(\{ t: '(\w+)'", bridge_js))
fwd_list = set(re.search(r"const FORWARDED = \[([^\]]*)\]", bridge_js)
               .group(1).replace("'", "").replace('"', "").split(", "))
fwd_list = {s.strip() for s in fwd_list if s.strip()}
want_fwd = {c["page"]["event"]: c["page"]["ws_message"]
            for c in controls if c["page"].get("ws_message")}
bad = [f"event '{e}': manifest says WS '{m}', bridge.js forwards "
       f"'{fwd.get(e)}'" for e, m in want_fwd.items() if fwd.get(e) != m]
bad += [f"bridge.js forwards event '{e}' (WS '{m}') claimed by no control"
        for e, m in fwd.items() if e not in want_fwd]
bad += [f"FORWARDED list in bridge.js disagrees with its MPC.on forwards: "
        f"{sorted(fwd_list ^ set(fwd))}"] if fwd_list != set(fwd) else []
session = {m["ws_message"] for m in manifest.get("session_messages_not_controls", [])}
sent_by_page = set(re.findall(r"send\(\{ t: '(\w+)'", bridge_js))
bad += [f"bridge.js sends WS message '{m}' claimed by no control and not "
        f"declared a session message" for m in sorted(sent_by_page - set(want_fwd.values()) - session)]
check("bridge.js: every control event is forwarded, every forward is "
      "claimed, session messages are declared", bad)

# ------------------------------------------------ 5. bridge.py WS dispatch
dispatch = set(re.findall(r'kind == "(\w+)"', bridge_src))
want_msgs = set(want_fwd.values())
bad = [f"manifest WS message '{m}' has no dispatch branch in bridge.py"
       for m in sorted(want_msgs - dispatch)]
bad += [f"bridge.py dispatches WS message '{m}' claimed by no control and "
        f"not declared a session message"
        for m in sorted(dispatch - want_msgs - session)]
check("bridge.py dispatch: every WS message handled, every handler claimed", bad)

# --------------------------------------- 6. bridge.py tables == manifest
bad = []
man_btn = {c["page_id"]: c for c in controls if c["kind"] == "button"}
for bid, pair in BUTTON_CODES.items():
    c = man_btn.get(bid)
    if c is None:
        bad.append(f"bridge.py BUTTON_CODES has '{bid}' but no manifest control")
        continue
    f = c.get("frame") or {}
    if (f.get("code"), f.get("code_shifted", f.get("code"))) != (pair[0], pair[1]):
        bad.append(f"button {bid}: manifest codes "
                   f"{f.get('code')}/{f.get('code_shifted')} != bridge.py {pair}")
for bid, c in man_btn.items():
    if c.get("frame") and c["frame"].get("pkt") == "BUTTON" and bid not in BUTTON_CODES:
        bad.append(f"manifest button {bid} claims a BUTTON frame but is "
                   f"absent from bridge.py BUTTON_CODES")
man_knob = {c["page_id"]: c for c in controls if c["kind"] == "knob"}
for kid, code in KNOB_CODES.items():
    c = man_knob.get(kid)
    if c is None or (c.get("frame") or {}).get("code") != code:
        bad.append(f"knob {kid}: manifest code "
                   f"{(c or {}).get('frame', {}).get('code')} != bridge.py KNOB_CODES {code}")
for kid in man_knob:
    if kid not in KNOB_CODES:
        bad.append(f"manifest knob {kid} absent from bridge.py KNOB_CODES")
jogpush = next(c for c in controls if c["id"] == "jog-push")
if (jogpush["frame"]["code"] != consts.get("EN_KNOB_ENTER")):
    bad.append(f"jog-push manifest code {jogpush['frame']['code']} != bridge.py "
               f"EN_KNOB_ENTER {consts.get('EN_KNOB_ENTER')}")
inv_led = {}
for code, pid in LED_CODE_TO_PANEL.items():
    inv_led.setdefault(pid, []).append(code)
for pid, codes in inv_led.items():
    c = man_btn.get(pid)
    if c is None:
        bad.append(f"bridge.py LED feedback targets '{pid}' — no manifest button")
    elif sorted(c.get("led_codes", [])) != sorted(codes):
        bad.append(f"button {pid}: manifest led_codes {c.get('led_codes')} != "
                   f"bridge.py LED_CODE_TO_PANEL {sorted(codes)}")
for c in controls:
    if c["kind"] == "button" and "led_codes" in c \
            and sorted(c["led_codes"]) != sorted(inv_led.get(c["page_id"], [])):
        bad.append(f"button {c['page_id']}: led_codes disagree with bridge.py")
check("bridge.py tables: BUTTON_CODES, KNOB_CODES, EN_KNOB_ENTER and "
      "LED_CODE_TO_PANEL all equal the manifest", bad)

# ------------------------------------- 7. frame shapes in bridge.py source
SHAPES = [
    ("PAD", 0, r"send_hw\(PKT_PAD, 0,"),
    ("KNOBS", 1, r"send_hw\(PKT_KNOBS, 1, \[KNOB_CODES\[kid\]"),
    ("KNOBS", 0, r"send_hw\(PKT_KNOBS, 0, \[0,"),
    ("BUTTON", 0, r"send_hw\(PKT_BUTTON, 0, \[EN_KNOB_ENTER,"),
    ("BUTTON", 0, r"send_hw\(PKT_BUTTON, 0, \[code,"),
    ("SLIDER", 0, r"send_hw\(PKT_SLIDER, 0,"),
]
bad = []
man_shapes = {(c["frame"]["pkt"], c["frame"]["cmd"])
              for c in controls if c.get("frame")}
for pkt, cmd, pat in SHAPES:
    if not re.search(pat, bridge_src):
        bad.append(f"bridge.py no longer sends the {pkt}/{cmd} frame "
                   f"shape ({pat}) the manifest describes")
    if (pkt, cmd) not in man_shapes:
        bad.append(f"no manifest control claims frame {pkt}/{cmd}")
extra = man_shapes - {(p, c) for p, c, _ in SHAPES}
bad += [f"manifest claims frame {p}/{c} but bridge.py has no such send shape"
        for p, c in sorted(extra)]
check("frame shapes: every manifest (pkt, cmd) is a real send_hw call "
      "in bridge.py, and vice versa", bad)

# --------------------------------------- 8. codes agree with the contracts
bad = []
for c in controls:
    f = c.get("frame") or {}
    for key, namekey in (("code", "code_name"), ("code_shifted", "code_name_shifted")):
        if key in f and namekey in f:   # only BtnLedSequence-domain codes carry names;
                                        # knob codes are qlink indices (see code_domain)
            want = BTN_BY_CODE.get(f[key])
            if f.get(namekey) != want:
                bad.append(f"{c['id']}: manifest says code {f[key]} is "
                           f"{f.get(namekey)} but dcl_contracts.h says {want}")
            if BTN.get(f.get(namekey), -999) != f[key]:
                bad.append(f"{c['id']}: dcl_contracts.h {f.get(namekey)} = "
                           f"{BTN.get(f.get(namekey))} != manifest {f[key]}")
if contracts_enum("KnobSequence")["EN_KNOB_CENTER"] != 0:
    bad.append("EN_KNOB_CENTER != 0 — jog rotary frame data[0] is stale")
if contracts_enum("KnobCmd") != {"EN_KNOB_ROTARY": 0, "EN_KNOB_QLINK": 1}:
    bad.append("KnobCmd enum changed — knob/jog cmd numbers need review")
if contracts_enum("PadCmd")["EN_PAD_STATE"] != 0:
    bad.append("EN_PAD_STATE != 0 — pad frame cmd is stale")
if contracts_enum("SliderCmd")["EN_SLIDER_TOUCH_INPUT"] != 0:
    bad.append("EN_SLIDER_TOUCH_INPUT != 0 — strip frame cmd is stale")
pkt_names = re.search(r"(PKT_\w+(?:, PKT_\w+)*) = range\(8\)", bridge_src).group(1)
pkt_types = {name[4:]: i for i, name in enumerate(pkt_names.split(", "))}
if manifest.get("pkt_types") != pkt_types:
    bad.append(f"manifest pkt_types {manifest.get('pkt_types')} != bridge.py {pkt_types}")
for c in controls:
    f = c.get("frame") or {}
    if f and f["pkt_type"] != pkt_types[f["pkt"]]:
        bad.append(f"{c['id']}: pkt_type {f['pkt_type']} != {f['pkt']} ({pkt_types[f['pkt']]})")
check("contracts: every named code in the manifest matches "
      "dcl_contracts.h, enum anchors unchanged", bad)

# ---------------------------------------------------------------- summary
kinds = {}
for c in controls:
    kinds[c["kind"]] = kinds.get(c["kind"], 0) + 1
print()
print(f"controls: {len(controls)} " +
      " ".join(f"{k}={v}" for k, v in sorted(kinds.items())))
gaps = [c["id"] for c in controls if "gap" in c]
print(f"declared gaps / page-local: {len(gaps)} ({', '.join(gaps)})")
if FAILURES:
    print(f"\nSELFTEST FAIL: {len(FAILURES)}/{CHECKS} check groups failed")
    sys.exit(1)
print(f"SELFTEST PASS: {CHECKS}/{CHECKS} check groups — every page control "
      f"maps to a live bridge path, every bridge path is claimed")
