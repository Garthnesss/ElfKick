# ElfKick — Synth Build Log (Oberheim-architecture 3-osc poly synth)

Build record for SYNTH_DESIGN.md's 7-step gated plan. Each step ends with a
gate verdict and the measured evidence behind it. Started Oct 29, 2026.

---

## Step 0 — MTProgramType consumer audit — **GATE: PASS**

Method: `grep -rn "MTProgramType|ProgramType::"` over `Kick/modules` +
`Kick/apps` = **287 hits across 55 files**; every switch/if-chain was then
read to its default/else (no verdict by grep alone). Additional sweeps for
consumers that don't name the enum: `switch(...getType())`,
`magic_enum`/`toEnum` casts, `(int)` type casts, array-index-by-type
(none found), contracts/IPC modules (no MTProgramType references at all —
the type travels inside the shared ValueTree document).

### Audit table (verdicts for a new type id `Synth = 8`)

| # | Consumer (file) | Behaviour for unlisted type | Verdict |
|---|---|---|---|
| 1 | Enum + `toString`/`fromString` (`interface/source/mt_audio_interface_types.h:97-107,236-264`) | defaults to "Unknown"/Unknown | **Registration touch-point** (add enumerator + both cases) |
| 2 | `VARIANT_CONVERTER` macro (`mt_audio_interface_types.h:38,218`) | `magic_enum::enum_cast(...).value()` — **throws** if int not in enum | **Hazard H1** — see below |
| 3 | Factory `MTAudioProgramManager::createProgram` (`mt_audio_manager_program.cpp:98-122`) | no branch → `prog == nullptr` → program silently skipped (safe) | **Registration touch-point** (add Synth branch) |
| 4 | Manager `setup()` / program list (`mt_audio_manager_program.cpp:57-72,232-244`) | iterates PROGRAM children; list is name-based | Safe |
| 5 | `MTIProgramData` (`mt_audio_interface_types.cpp:395-415`) | `type` CachedValue, Unknown default; all fields generic | Safe |
| 6 | `MTIPadColors` ctor (`mt_audio_interface_types.cpp:355-380`) | `magic_enum::enum_cast<MTProgramType>((int)type).value()` at :361; colour table: Drum→drum, Clip→clip, else default pad colours | **Hazard H1** (throw) + safe colour default after registration |
| 7 | Interface sample-op switches ×6 (`mt_audio_interface.cpp:358,371,384,405,459,477` — setSimultaneousNotePlay/Mute, addSample, removeSample, copyPadDataToPad ×2) | no `default:` — fall-through, silent no-op | Safe-by-default |
| 8 | `MTAudioInterface::padAddToGroup/padRemoveFromGroup` (`mt_audio_interface.cpp:695-713`) | **not type-gated** → calls `setNoteMuteGroup` (unguarded deref, #10) | **Hazard H2** (shared-stock) |
| 9 | Manager per-note setters `setSemi/setPan/setFineForSelectedProgram` (`mt_audio_manager_program.cpp:~300-330`) | per-note variants (Index ≥ 0) call `setNoteSemi/setNotePan/…` ungated by type | **Hazard H2** (shared-stock) |
| 10 | `MTIProgram` per-note sample APIs (`mt_audio_iprogram.cpp:390-400,451-486,580`) | `getOrCreate(Note)->…` **dereferenced without null check** in getSimultaneousNotePlay/Mute, setSimultaneousNotePlay/Mute, setNoteMuteGroup/Min/Key/Max/Semi/Fine/Gain/Pan, getLayerSounds. `getOrCreate` returns nullptr when `iSampleCreator` returns nullptr (base default, `mt_audio_iprogram.h:274-275`) | **Hazard H2 — root cause.** Pre-existing for stock **Midi/Cv/Audio** (none override `iSampleCreator`); type-aware callers all gate correctly (#7, #20, #24); only generic pad/sample-oriented flows (#8, #9, #22) can reach it |
| 11 | `MTIProgram` guarded sample paths (`getLayer`, `getLayerSourceSlice`, `createSample`, `rebuildSampleMap`, `removeSample`, `getSamples`, `getNoteVT`, `MTCommonProgram::createSample/refresh`) | all null-checked / empty-map safe | Safe |
| 12 | `MTCommonProgram::getNoteSamplePlay` (`mt_audio_common_program.h:71`) | returns ONE_SHOT; base `MTIProgram` returns NOTE_ON; `noteProcess` dispatches on the virtual (`mt_audio_iprogram.cpp:261-310`) | **Registration requirement**: MTSynthProgram must override → NOTE_ON (as designed) |
| 13 | `kick_leds.cpp` `changePadColours` (`:366-425`) | switch: Drum/Audio/Clip → sample-based colours; Keygroup/Midi/Plugin/Cv → keygroup colours; **no default** → for Synth (and Unknown today) **no pad colour/state command is sent at all** | **Needs-a-case (H3)** — stability-safe; pads keep previous state; pad *input* unaffected. Optional follow-up: add Synth to the Keygroup-family case |
| 14 | `ProgramEdit::loadProgram` (`kick_ProgramEdit.cpp:149-230`) | no case → `selectedProgram == nullptr` → all editors hidden, info label shown; handleCommand null-guarded | Safe (no editor in v1, as designed) |
| 15 | Per-type editor classes (`program_edit_ui/sources/*`) | constructed with fixed type constants; only reached via #14's switch | Safe (inert) |
| 16 | Create-program menu (`kick_AppMainMode.cpp:33-39`) | fixed buttons per type; no Synth button | Safe (v1 programs are generated, as designed) |
| 17 | `AppProgramComponent` display + edit routing (`kick_AppProgramComponent.cpp:62-68,170,196-290`) | display switch has `case Unknown: default:` (name only); edit click matches no branch → no-op | Safe |
| 18 | `AppGrid::setViewType` (`kick_AppGrid.cpp:468-485`) | no default → grid view type keeps its previous value for a synth track | **Needs-a-case, optional (H4)** — one line: Synth → Keys/MidiView (semantically correct for a melodic program); not gate-blocking |
| 19 | Program-selector icons ×3 (`kick_programSelector.cpp:32-80`) | all have `default:` → generic icon names | Safe |
| 20 | `KnobGrid` per-note edits (`kick_KnobGrid.cpp:170-210`) | gated `Drum\|\|Clip\|\|Keygroup`; Midi early-returns; others never reach per-note setters | Safe |
| 21 | `KnobTabs` (`setListInfo` if-chain, `TopContent::click` switch w/ default, `MiddleContent::updateButtons` switch w/ default-first) (`kick_KnobTabs.cpp:82-360`) | unmatched type → no list options / generic buttons | Safe |
| 22 | `KnobEdit` (`kick_KnobEdit.cpp:18,85-98`) | private label switch has `default:` → "Unknown Program" for Synth (independent of `toString`) | Safe; **cosmetic (H5)** — optional case → "Synth Program" |
| 23 | `MidiControlTabs` (`kick_MidiControlTabs.cpp:1936,2023-2280,2598-2700`) | if/else chains end without final else (Plugin case empty); CC-learn per-note edits are `type == Clip` + dynamic_cast gated | Safe |
| 24 | `PadMixer` + drum/keygroup pad-mixer headers (`kick_PadMixer.cpp:416`, `kick_PadMixerDrumProgram.h:85`, `kick_PadMixerKeygroupProgram.h:61`) | Drum/Clip-gated or type-conditional components | Safe |
| 25 | Pad-mute screen (`kick_padMute.cpp:138-235`) | data/LED paths gated Drum / Drum\|\|Clip; group-assign handler calls `setNoteMuteGroup` ungated → **H2 class** | Hazard H2 (shared-stock) |
| 26 | Assign-sample dialog (`kick_AppAssignSample.cpp:92-111,211,362-366,423-445`) | assign path gated Drum/Clip/Keygroup; release switch has `default: → noteOff` (correct for a synth); prefix switch has `default:` | Safe |
| 27 | Sample editor (`kick_SampleEditor.cpp:147,622`) | Drum-only checks | Safe (excluded) |
| 28 | Sampler / Looper admission (`kick_Sampler.cpp:233`, `kick_Looper.cpp:60`) | admit only Drum/Keygroup/Clip/Audio | Safe (exclusion is correct — sample tools) |
| 29 | File utils / pad-colour screens (`kick_fileUtils.cpp:97`, `kick_PadColor.cpp:60,246`) | Keygroup-only special cases; colour selection forwards to #13 | Safe |
| 30 | Track view / track options / channel-mixer icon plumbing (`kick_AppTrackComponent`, `kick_AppTrackOptions`, `kick_ui_channel_mixer/*`) | pass type to #19's icon fns or store it opaquely | Safe |
| 31 | Sequence engine (`mt_audio_sequence.cpp:475,498`) | `getType() != Audio` gates (clip-launch behaviour) | Safe — Synth is treated as a note program, which is correct |
| 32 | Interface category listing (`mt_audio_interface.cpp:630-632`) | Audio vs non-Audio buckets | Safe — Synth lands in the non-Audio bucket |
| 33 | `MTIProgram::isDrum/isKeys/isCV/isMidi/isClip/isAudio` (`mt_audio_iprogram.h:84-89`) | all false for Synth; every caller uses them as positive gates | Safe |
| 34 | Contracts / IPC / DCL | no MTProgramType references anywhere | Safe |
| — | Non-consumers verified (different enums): asset manager (`AssetTypes`), song transport switch (`EN_PLAY…`), step-preset switch (`StepPresetType`), NanoLog | — | Excluded |

### Hazards + handling plans

- **H1 — enum registration is load-bearing (crash class).**
  `magic_enum::enum_cast<MTProgramType>(...).value()` (rows #2, #6) throws
  `std::bad_optional_access` if a project file carries a type int that the
  running binary's enum doesn't declare. Handling: the `Synth = 8`
  enumerator + factory + strings land in the **same build** as any type-8
  content, and **all six app binaries are rebuilt together** (design Step 3
  gate already requires this) — a stale binary must never load ElfSynth01.
  No code change beyond registration.
- **H2 — unguarded `getOrCreate()->` derefs (shared-stock, pre-existing).**
  Rows #8/#9/#10/#25. Stock Midi/Cv/Audio programs carry the identical
  exposure today (null `iSampleCreator`); Synth introduces no new class of
  risk. Handling: MTSynthProgram inherits the nullptr creator (stock
  precedent); synth code never calls per-note sample APIs; the in-rig proof
  exercises select/play/switch/mixer/LED paths but deliberately does **not**
  exercise pad-mute-group assignment or per-note pitch edits on the synth
  program (documented exclusion). A generic null-guard patch to MTIProgram
  is recommended as a *separate* upstream-hardening change — explicitly
  **not** part of registration.
- **H3 — pad LED colours not updated for Synth (functional, cosmetic).**
  Row #13. Handling: accepted for v1; optional one-case follow-up (Synth →
  Keygroup family) if the in-rig proof finds stale pads confusing.
- **H4 — sequencer grid view (minor UX).** Row #18. Recommended one-line
  case at registration (Synth → Keys view).
- **H5 — knob-edit label (cosmetic).** Row #22. Optional one-line case at
  registration.

### Definitive registration touch-point list

1. `Kick/modules/engine/interface/source/mt_audio_interface_types.h` —
   `Synth = 8` enumerator (after `Unknown = 7`), `"Synth"` in
   `toString`/`fromString`.
2. `Kick/modules/engine/audio_engine/engine/mt_audio_manager_program.cpp`
   — factory branch `if (type == MTProgramType::Synth) prog = new MTSynthProgram(...)`.
3. `Kick/modules/engine/interface/source/mt_audio_ids.h` — new
   `DECLARE_ID`s: `SYNTH`, `OSC`, and any of `wave`/`glide`/`spread`/
   `baseNote` not already present (verify at implementation).
4. `Kick/modules/engine/audio_program/audio_program.cpp` — unity includes
   for the three new .cpp files.
5. New files: `source/mt_audio_synth_program.h/.cpp`,
   `source/mt_audio_synth_atomics.h`, `synth/synthProgram.h` +
   `synth/SynthProgram.cpp`, `synth/synth_voice.h/.cpp`,
   `synth/polyblep_oscillator.h`.
6. Optional cosmetic cases (H4 recommended, H5 recommended, H3 deferred):
   `kick_AppGrid.cpp` setViewType, `kick_KnobEdit.cpp` label,
   `kick_leds.cpp` changePadColours.

**Step 0 gate: PASS** — no unexamined consumer remains (287 hits / 55
files, every switch read to its default); hazards H1–H5 named with
handling plans; touch-point list is definitive.

---

## Step 1 — PolyBLEP oscillator + deterministic render harness — **GATE: PASS**

The one written component: `audio_program/synth/polyblep_oscillator.h`
— header-only, JUCE-free, double-precision phase, saw/pulse(variable
width)/triangle/sine, PolyBLEP residuals formulated exactly as Garth's
house precedents (elfsynth-core `osc.rs`, ElfPro800 `PluginProcessor.cpp`).

Harness: `scripts/synth_harness.cpp` (standalone g++, pumps `process()`
manually — no device) → WAV renders; `scripts/analyze_synth_render.py`
(numpy FFT) asserts. Measured results (48 kHz):

| Assertion | Measured | Gate |
|---|---|---|
| Fundamental, all 5 waveforms | **440.02 Hz** | 440 ±1% |
| Saw H2..H6 vs 1/n | 0.525/0.361/0.232/0.215/0.174 vs 0.500/0.333/0.250/0.200/0.167 (worst dev. 7.5%) | ±30% |
| Pulse 50% H2 null | **−159.4 dB** | < −25 dB |
| Pulse 25% H2 present | −2.6 dB | > −12 dB |
| Pulse 25% H4 null | **−175.5 dB** | < −20 dB |
| Gated-off render | max\|sample\| = 0.000000 (24,000 samples) | exact 0 |
| Saw tail after gate-off | max\|sample\| = 0.000000 | exact 0 |
| Saw steady-state RMS | −4.88 dBFS | [−8, −3] dBFS |
| CPU soft check | 16.0 s of saw rendered in 3 ms wall | log only |

**16/16 assertions passed.** Step 1 gate: PASS (oscillator level; the
full-voice render proof remains design Step 2, gated separately).

---

## Steps 2+3 — voice, program classes, registration (in progress)

Written (all marked ElfKick additions, AGPL headers):

- `audio_program/source/mt_audio_synth_atomics.h` — MTIAtomicSynthParams
  (osc/envelope/filter atomics on the SYNTH subtree). **Deviation from
  §5.4, forced by the stock code:** the synth does NOT reuse
  MTIAtomicEnvelopes — MTIAtomicFilterEnvelope attaches two atomics to
  the same `type` property (its own + MTIAtomicADSR's adsrType) and
  types cutoff/resonance as ints (default 20000 here vs 8500.0 in
  DrumProgram's factory XML). The synth's own structs keep the stock
  property names and the §6.2 units (float seconds, float Hz, float Q).
  MTIAtomicLFO IS reused unchanged, as designed.
- `audio_program/synth/polyblep_oscillator.h` (Step 1), `synth_voice.h/.cpp`
  (3× PolyBLEP → juce TPT SVF per-sample modulated → amp ADSR; glide as
  smoothed log2 freq; deterministic spread; steal quick-release),
  `synthProgram.h` + `SynthProgram.cpp` (16-voice pool, retrigger/free/
  steal allocation with pending-note queue resolved in doProcess),
  `source/mt_audio_synth_program.h/.cpp` (MTSynthProgram mirroring
  MTDrumProgram minus sample machinery; getNoteSamplePlay → NOTE_ON).
- Registration: `Synth = 8` + toString/fromString; IDs SYNTH, OSC, wave,
  width, glide, spread; factory branch in the manager; unity + module
  header includes; cosmetic cases H4 (AppGrid → Keys view) and H5
  (KnobEdit → "Synth Program") applied; H3 (kick_leds) deferred per plan.
- `apps/synth_harness/` — the design Step 2 console target (constructs
  SynthVoices directly, pumps process() offline); `scripts/make_synth_project.py`
  (ElfSynth01 generator, §6.1 XML verbatim + SMF triad demo);
  `scripts/analyze_synth_voice.py` (Step 2 measurement gate).

**Build finding (matters):** `juce::juce_dsp` was NOT linked by
KickAudioEngine/KickAudioModel (kick, kick_gui, kickem link it). The
engine target initially linked anyway — LTO pruned the unresolved TPT
references — but the harness (which calls SynthVoice directly) failed
to link with undefined `StateVariableTPTFilter<float>` references.
Fix: `target_link_libraries(... juce::juce_dsp)` added to
kick_audio_engine, kick_audio_model and synth_harness CMakeLists.
KickAudioEngine compiled the full synth unity cleanly before this fix;
the Step 4/6 functional gates remain the arbiters for the rig binaries.

**SEMI clamp note (from the audit trail):** MTIAtomicProgramMaster's
semi atomic clamps WRITES to ±12 (VTRangedPropertyValueSource::setValue),
but the READ path is unclamped — the generated project's authored
semi="36" is honoured on load (verified in kick_atomics.h). A GUI
transpose edit would clamp it to 12; documented behaviour, not a bug
for the authored-content workflow.

**KickAudioEngine: BUILT GREEN** with all synth code + registration
(8,476,680 B artefact). Harness + engine/model relink with juce_dsp in
progress at the time of writing; Step 2 measurements follow.

## Step 2 — offline render proof (design Step 2) — **GATE: PASS**

SynthHarness (apps/synth_harness) constructs SynthVoices directly
against the §6 schema tree and pumps process() at 48 kHz/960.
Renders kept at `synth-voice-renders/`; gate script
`scripts/analyze_synth_voice.py`. Final run: **14/14 assertions pass.**

| Assertion | Measured |
|---|---|
| Fundamental (osc 1 only, note 69) | 440.02 Hz FFT / 440.0 Hz zero-crossing |
| Saw H2..H6 through the voice | 0.525/0.361/0.232/0.215/0.174 vs 1/n (worst dev. 7.5%) |
| Sustain RMS (gate held, 0.6–1.1 s) | **−19.00 dBFS** (gate > −20) |
| Post-release (release 0.30 s, checked 1.9–2.0 s) | −240 dBFS (digital silence) |
| Triad 60/64/67 components | −0.3 / −0.5 / 0.0 dB rel band max |
| Filter centroid, cutoff 200 vs 12000 Hz | 1130 Hz vs 4676 Hz (ratio 0.24, gate < 0.5) |
| Full-patch A440 peak | 0.478 (gate < 1.2) |
| CPU | 9.3 voice-seconds in 237 ms = 2.55% of one core |

Two first-run failures, both diagnosed before fixing:
1. Sustain RMS −20.25 dBFS vs the −20 floor → the design-sanctioned
   mixer tune: kMixScale 0.5 → 1/√3 ≈ 0.577. (Full-patch triad render
   peaks at 1.128 float — three full voices summed; the program master
   gain is the trim, as on the drum program.)
2. Triad C component read −17.4 dB in a single FFT window — a
   measurement artefact, not a voice defect: the default patch's
   +7-cent osc-2 detune beats at ~1.06 Hz at 261.63 Hz and the window
   sat in the beat null (|110−90|/200 ≈ −20 dB — matches). The gate now
   measures sustain-averaged spectra (beat-phase invariant); all three
   fundamentals land within 0.5 dB. The patch was NOT changed to pass.

## Step 3 — classes + factory + registration — **GATE: PASS**

KickAudioEngine, KickAudioModel and SynthHarness build green with the
synth included; juce_dsp.cpp.o is now compiled into the engine/model
targets (verified in the target object tree). The remaining app
targets (Kick, KickUserInterface, KickMain, KickDeviceControl) are
building now; this verdict completes when they land.
Regression surfaces (AudIO examples / KT_KickEM) are untouched by this
tranche's diff except the shared interface module (enum + IDs are
append-only) — the in-rig drum non-regression in Step 6 is the live
check.

**VM RESTART (~22:56 PDT, environmental):** the VM rebooted mid-build —
/tmp wiped (Step 1 render WAVs + harness binary lost; their measured
numbers are recorded above and the sources are in the repo), the
running rig (KickMain set + bridge + Xvfb) is down, and /app +
/home/Kick staging was wiped. All synth sources, build trees and this
log (under ~/workspace) survive. Builds resumed with logs kept under
~/workspace/elfkick/ from here; the rig will be restaged per
RUNTIME_LOG.md before Steps 4/6.

## Step 4 — model load test (ElfSynth01 in the rebuilt rig) — **GATE: PASS**

Rig restaged per RUNTIME_LOG.md with the Step 3 binaries (engine
8,510,176 B; model 8,447,576 B; GUI 23,380,280 B; all Oct 30 builds).

- ElfSynth01 loads both ways: File Browser
  (/home/Kick/projects/ElfSynth01/ElfSynth01.kickPrj) and the Projects
  home tile. Loaded three times across the session; all five rig
  processes stay alive throughout.
- The GUI shows the program with its authored identity: Track 1
  "Synth / Elf Synth" (keys-grid track icon, not the drum icon),
  bottom strip SEQUENCES "Sequence 01" / TRACKS "Synth" / PROGRAMS
  "Elf Synth". The factory created the program from type="8" and the
  track linked by id="ElfSynth1" — the registration works end to end.
- **Re-save preservation (the design's explicit check): PASS.** On a
  project switch the app rewrote ElfSynth01.kickPrj (1,351 → 2,290 B —
  the same normalisation growth ElfTest01 shows, 3,608 → ~6.4 KB).
  The rewritten file's `<SYNTH>`, `<ENVELOPE>` and `<LFO>` subtrees
  are **byte-identical** to the authored ones, and the program stays
  type="8" / name "Elf Synth" (verified by structural diff against
  the generator's KICKPRJ constant).
- The app also re-serialised the demo pattern on save:
  `Sequence 01/Sequence 01.mid` 156 → 296 B (JUCE MidiFile writer
  form); the pattern content survives the round trip.
- Close/reload: switching ElfSynth01 ↔ ElfTest01 with the transport
  stopped works repeatedly (see the stock hazard below for the
  playing case).

## Step 5 — generator emits ElfSynth01 — **GATE: PASS**

`scripts/make_synth_project.py` → `/home/Kick/projects/ElfSynth01/`:
program "Elf Synth" type="8" id="ElfSynth1", program-master
semi="36", the §6.1 SYNTH subtree verbatim, demo
`Sequence 01/Sequence 01.mid` (C–F–G–Am triads at notes 60+, one bar
each, 120 BPM). Pitch arithmetic, stated honestly in the generator:
with semi=36 the pads (notes 0–15) sound 65.4–155.6 Hz, and the demo
chords (notes 60+) sound ~2.1–3.1 kHz — the design's mechanism was
followed literally. The generator has no output-dir argument; it
always writes /home/Kick/projects/ElfSynth01 (learned the hard way
mid-session — it restored the authored .kickPrj over the app's
re-save during a check; the re-save verdict above was captured
before that, on the rewritten file itself).

## Step 6 — in-rig proof — **GATE: PASS** (one sub-check blocked by a stock rig limitation, recorded)

**Drum non-regression (run first, per the tranche order): PASS.**
On the rebuilt binaries, ElfTest01 pad 1 press → track meter ~75%
at +150 ms, decayed to residual by +1 s (screenshots
`synth-drum-meter-lit.png` / `synth-drum-meter-decayed.png`) — the
Phase 3 gate signature, unchanged. The PCM capture below shows the
same hit decaying within ~0.25–0.5 s (`rig-captures/drum-hit.wav`).

**Synth attack: PASS.** Pad press → track meter ~75–80% at +150 ms
(`rig-captures/synth-meter-attack.png`).

**Synth sustain: PASS — proven from the engine's rendered output.**
The GUI track meter is NOT a faithful witness for held notes: it
decays visually on constant input (and once rose after a release),
which produced a false "voice dies at ~1.5 s" reading mid-session —
retracted; see Corrections. The decisive measurement is the
engine's own PCM, captured by pointing ALSA at a file-plugin FIFO
with a throttled reader (`asound-fifo.conf` +
`scripts/rig_capture_reader.py`; the engine negotiates float32,
4 channels, 48 kHz). Pad 1 held for exactly 3.0 s of gate:

- silence before the note; attack at gate start (RMS 0.105 → 0.141);
- **RMS 0.103–0.141 sustained for the whole 3.0 s gate**, with the
  gentle undulation of the +7-cent osc-2 beating;
- noteOff → the 0.30 s release tail completes inside the next
  0.25 s window; digital silence after;
- spectrum in sustain: 32.7 Hz sub fundamental (osc 3 at −12 st),
  ~65.4 Hz saw/pulse fundamentals and their harmonic series.

Extract: `rig-captures/synth-hold-3s.wav` (1 s pre-roll + the hold +
tail). Sustain-vs-decay contrast with the drum is exactly the
design's success shape: NOTE_ON voices gate; ONE_SHOT drums decay.

**Demo-sequence playback: NOT EXECUTED — stock rig limitation.**
The pattern loads (Sequence page reads 4 bars / 120 BPM; the file
survives an app save round trip), and the transport counter
advances when played — but sequence playback emits **no program
output in this headless rig for any program**: a control pattern
written into ElfTest01 (kick on every beat, 4 bars) is equally
silent, meters dark at every beat for both programs. Live pad input
works; the sequencer's note dispatch to programs does not happen in
this environment (no stock program sounds from the sequencer here
either). Recorded as a pre-existing rig/stock limitation, not a
synth defect; the demo.mid itself is verified well-formed by
construction and by the app's load/save round trip above.

**Stock hazards found while testing (all program types, not synth
defects):**
- Switching projects while the transport plays is unsafe in the
  stock app: once refused with a "Selected file cannot be loaded"
  dialog, once the GUI exited cleanly (exit code 0; KickMain
  restarted it and the tree then shut down). Switch only when
  stopped.
- H2 exclusions respected throughout: no pad-mute-group assignment
  and no per-note pitch edits were performed on the synth program.

## Step 7 — docs + commit

This log; STATUS.md, ROADMAP.md, PROJECT_FORMAT.md and
RUNTIME_LOG.md updated (re-save behaviour, the DCL/pty remedy, the
meter caveat, the capture recipe). Tranche committed to the Mac
clone via the tar/remote-storage route; Garth pushes.

## Corrections to earlier entries

- Step 3's "the remaining app targets are building now; this
  verdict completes when they land" is superseded: all six targets
  (SynthHarness, KickAudioEngine, KickAudioModel, Kick, 
  KickUserInterface, KickMain, KickDeviceControl) finished green,
  EXIT:0 (`synth-build-3.log`, `synth-build-4.log`).
- The mid-session Step 6 observation "held notes die at ~1.5–2 s"
  is **retracted**: the GUI meter decays on constant input. The
  engine PCM (above) proves full sustain. Any future in-rig level
  claim in this project needs the capture rig, not screenshots of
  meters.
- Rig lesson (also in RUNTIME_LOG.md): after a KickDeviceControl
  dies on the bridge pty, its TIOCEXCL persists while the bridge
  holds the master, and later DCLs silently fail to open the port
  ("Serial Not Found", no pad path, no error surfaced in the GUI).
  Remedy: restart the bridge (fresh pty) BEFORE (re)starting the
  rig/DCL. Several interim rigs in this session had a dead pad path
  for this reason; every conclusion drawn in those windows was
  re-verified after the fix (the first all-zero capture attempt was
  one casualty — no note had ever been triggered into it).

## Tranche verdict

Steps 0–7 complete. Step 0 audit PASS (287 hits / 55 files, hazards
H1–H5 named and handled); Step 1 oscillator + harness PASS (16/16);
Step 2 offline voice proof PASS (14/14); Step 3 registration +
builds PASS (all targets green); Step 4 load test PASS (identity +
byte-identical re-save preservation); Step 5 generator PASS;
Step 6 in-rig proof PASS (drum non-regression, synth attack +
PCM-proven sustain; demo-sequence playback blocked by a stock rig
limitation that affects all programs equally). The synth is a
working, loadable, playable program type in the real runtime.
