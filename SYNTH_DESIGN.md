# ElfKick — 3-Oscillator Poly Synth for the Kick Software: Recon + Design

**Status:** DESIGN ONLY (no code written). Oct 29, 2026.
**Direction (Garth):** a native (C++/JUCE, engine-layer) 3-osc poly synth that runs
inside the Kick software — developed/tested in the emulator, portable to the
device build unchanged — architecturally based on the **Oberheim SEM voice**,
following Garth's established synth-research precedents (§3).

All file:line citations are to the ElfKick tree (`~/workspace/elfkick`) unless a
path says otherwise. The AudIO trees of upstream 0700-AAM and sibling 0716-AAF
are code-identical (Oct 28 diff: header-only differences), so nothing below
differs between them.

---

## 1. Inventory — what synth machinery already exists

### 1.1 AudIO DSP primitives (`AudIO/AudIO/Dsp/`, `Includes/idsp.h`)

All AudIO DSP classes derive from `AudIO::IDSP<T>` — `setSampleRate()` (which
triggers `recalculate()`), `setEnabled()/setBypass()` (both default **off** —
a DSP object renders nothing until enabled), per-sample `process()` and block
`process(Buffer&)` (`AudIO/AudIO/Includes/idsp.h:31-58`).

| Primitive | What it is | Quality verdict |
|---|---|---|
| `AudIO::LFO` (`Dsp/lfo.h`, `Dsp/lfo.cpp`) | **A full oscillator**, despite the name: 1024-entry wavetables (sine/saw/triangle built in `lfo.cpp:47-61`), square from phase compare (`lfo.cpp:212-215`), noise + sample&hold (`lfo.cpp:216-224`), frequency 0.001–20000 Hz clamped (`lfo.cpp:72-76`), phase increment = freq/sampleRate (`lfo.cpp:99-100`), plus delay/fade-in and S&H duration (`lfo.h:40-51`). Has a **PolyBLEP path**: `process_polyBlep()/advance_polyBlep()` layering BLEP residuals on saw/triangle/square (`lfo.cpp:245-264`, `poly_blep` at `lfo.cpp:117-141`). | Usable, with caveats: the table lookup is **nearest-neighbour as compiled** (`USE_INTERPOLATION (0)` and the interpolation branch gated `> 0` → compiled out, `lfo.cpp:29,180-193`), so the plain path aliases/steps at audio rates; the polyBLEP path fixes the discontinuities that matter for saw/square. Square is fixed 50 % duty — **no pulse width, no PWM**. Returns `1.f` when disabled (`lfo.cpp:168-169`) — a trap if misused as an audio osc. Verdict: **right tool for the LFO job** (sub-audio, where its quality is ample and delay/fade/S&H are bonuses); **not the SEM VCO** (no PWM). |
| `AudIO::ADSR<T>` (`Dsp/adsr.h`) | Full **A/H/D/S/R** envelope, exponential (default) or linear curves, per-sample `process()`, block form that multiplies a buffer, `gate(bool)/keyOn()/keyOff()`, stage times in **seconds** (`calcSamplesToState` multiplies by sampleRate, `adsr.h:196-208`). Defaults A 0.01 / H 0 / D 0.5 / S 0.1 / R 1.0 (`adsr.h:258`). | **Compose.** This is the SEM's EG shape (two of them: amp + filter). Used by the Kick voices via the `mt::ADSR` wrapper (§1.3). |
| `AudIO::Filter<T>` (`Dsp/filter.h`) | LP/HP/BP from a cascade of one-poles (`buf0..buf3`), 12/24 dB slope select, resonance via a feedback fudge (`filter.h:104-129,158-161`). Cutoff is **normalised 0.01–0.99, no Hz mapping** (`filter.h:74-83`). | **Do not use.** It is *not* a state-variable filter (no integrator pair, no simultaneous modal taps — the "modes" are arithmetic on the cascade). Worse, it's bit-rotted: `setParameters(const Params&)` **silently discards its argument** (only calls `recalculate()`, `filter.h:71`), and `reset()` doesn't clear the state buffers (`filter.h:97`). Our own reconstruction notes say the same (`AudIO_juce.h:129-131`). |
| `AudIO::Panner<T>` (`Dsp/pan.h`) | Stereo panner; per-voice in `DrumVoice` (`drum_voice.h:61`) and as `PannerFloat` in the program master (`mt_audio_iprogram_atomics.h:63`). | **Compose** (per-voice pan, exactly as DrumVoice does). |
| — | **No dedicated audio-rate oscillator class exists in AudIO** other than `LFO` above and the `SineWave` object below. No wavetable synth, no VA oscillator. | The VCO is the one part we write (§3, §5). |

### 1.2 AudIO objects (`AudIO/AudIO/Objects/`) — the `AudIO::Object` graph layer

- `SineWave` (`SineWave.h:15-46`, `SineWave.cpp:131-158`): an `Object` wrapping
  one `AudIO::LFO` set to SINE; params Bypass/Freq/Amplitude/Phase;
  `REGISTER_OBJECT("SineWave", …)` for factory creation by name. Monophonic,
  no envelope.
- `SineWaveADSR` (`SineWave_adsr.h:13-41`): `SineWave` + one `AudIO::ADSR<float>`
  with `keyOn()/keyOff()` — the closest thing to a synth voice in AudIO, still
  sine-only and monophonic.
- `SFSynth` (`SFSynth.h:23-70`): **the only true polyphonic synth object in
  AudIO** — TinySoundFont player: `channelNoteOn/Off(pitch, velocity, channel)`,
  program change, CC, pitch bend, rendered in `process()`. Sample-based
  (SoundFont), and its GM font is stubbed out of the release — not a VA path.
- `ADSRObj` (`Objects/ADSR.h`): the DSP ADSR wrapped as a graph Object
  (gate on/off, seconds) — a utility, not a voice.
- `WhiteNoise` (`WhiteNoise.h`, 36 lines), `Mixer`, `VolumeAndPan`, `LevelMeter`,
  `PlayBack`, `Recorder`: graph utilities; `LevelMeter` is what feeds the
  render-proved meters.
- `AudIO/AudIO/Example/Synth/Main.cpp`: an ImGui/GLFW **desktop GUI** app that
  plays a MIDI file (`venture.mid`, TinyMidiLoader `tml.h`) into `SFSynth`
  (`Main.cpp:13,29,44-68,212-220`). Desktop-only, SoundFont-only — a proof that
  objects are created by name in a `Design`, not a synth engine we can reuse.

### 1.3 The Kick layer already has a synth architecture (`Kick/modules/engine/audio_program/synth/`)

This is the important discovery: the Kick engine is *already* a synth framework
with exactly one working instrument (drums) plugged into it.

- `mt::ISynthObject` (`iSynthObject.h:24-84`): an `AudIO::Object` +
  `DawRouting` + `DawInserts` + `IMidiMsgProcessor`. Owns a pre-allocated voice
  pool, per-program `MTIAtomicProgramMaster` (gain/pan/semi/fine/poly/mute/solo
  attached to the program's ValueTree), output metering. Its `process()`
  (`iSynthObject.cpp:189-212`) clears the buffer, calls every voice's
  `process()`, runs inserts, applies master gain, meters — **this is the render
  path whose meters proved the Drum program end-to-end in the pad-gate test.**
- `mt::ISynthVoice` (`iSynthVoice.h:26-73`, `iSynthVoice.cpp`): voice state
  machine (FREE/SETUP/RUNNING/STOPPING, atomics), velocity gain ramp, note
  number, `sampleRate` member (default 48000 — set properly in
  `prepareToPlay`), a 2×`AUDIO_BLOCK_SIZE` temp buffer. It is built on
  `ASliceRenderer` (the sampler renderer) — slice-centric, but the ctor accepts
  a null slice (`iSynthVoice.cpp:27-31`) and `DrumVoice` shows the pattern of
  overriding `noteOn/noteOff/process` wholesale.
- **Voice allocation** (`iSynthObject.cpp:95-112`): `getFreeVoice(Note,
  Polyphony)` counts voices playing *that note* against the passed per-note
  limit, then returns the first FREE voice. The "Steal voice" branch is an
  **empty comment** — when the pool is exhausted the note is silently dropped.
  Any stealing policy is ours to add in the subclass (§5.3).
- `mt::DrumVoice` (`drum_voice.h`, `drum_voice.cpp`) — **the voice template.**
  Per-sample loop (`drum_voice.cpp:180-212`): LFO + three envelopes
  (amp/filter/pitch) evaluated per sample; mod matrix in miniature
  (LFO→amp/pan/pitch/filter, env→filter/pitch, velocity→amp/pan);
  `updateFilter(fMod)` per sample; source sample from the slice renderer
  (`getNext(pMod)`); per-voice `AudIO::Panner`; master L/R gains; adds into the
  output buffer; `hasFinished()` → `makeFree()`. Its DSP blocks are
  `AudIO::LFO`, `AudIO::Panner<float>`, and the `mt::` wrappers below.
- `mt::DrumProgram` (`synth/DrumProgram.cpp:29-49`): `ISynthObject` subclass;
  ctor passes `POLYPHONY::MAX` (=16, `AudIO_constants.h:66-72`), pre-creates 16
  `DrumVoice`s, `addVoices()`. `noteOn()` does mute-group/layer logic, then
  `getFreeVoice()` → `voice->setSlice()` → `voice->noteOn()` →
  `addVoiceForPlayback()`.
- `mt::KeygroupProgram` (`synth/KeygroupProgram.cpp:58-67`): **its `noteOn()`
  is a stub** — mute check, then `return;` with the entire implementation
  commented out (and the commented code references a pre-refactor API that no
  longer exists). *There is no working melodic instrument in the released
  code.* Our synth is the first; the Drum path is the only live reference.
- `mt::AudioProgram`/`ClipProgram` (`synth/audioProgram.*`, `clipProgram.*`):
  clip-playback instruments on the same base — not note synths.

**The `mt::` DSP wrappers** (`AudIO/AudIO_juce/AudIO_juce.h` — the private
AudIO_juce module, reconstructed in ElfKick Phase 2 from upstream call sites;
the *API surface* is attested by upstream's own `drum_voice.cpp` calls):

- `mt::ADSR : AudIO::ADSR<double>` with the 5-scalar `setParameters(A,H,D,R,S)`
  setter and `operator()` (`AudIO_juce.h:113-124`).
- `mt::Filter` wraps **`juce::dsp::StateVariableTPTFilter<float>`**
  (`AudIO_juce.h:131-183`): LP/BP/HP, cutoff in **Hz** via a
  `NormalisableRange 20–20000` (skew 0.25), resonance clamped to Q 0.1–10.
  This — not `AudIO::Filter` — is what `DrumVoice` calls `Filter`
  (`drum_voice.cpp:107-114`). Note one mismatch: `drum_voice.cpp:200` calls
  `filter.process(sample)` per-sample-frame, an overload our reconstruction
  doesn't have (it has block `process(AudioBuffer&)`) — the private module's
  exact API is partly inferred. Our voice will use `juce::dsp` directly (§3),
  so this doesn't block us.

### 1.4 The program (model) layer — where a new instrument registers

Two-class pattern per instrument, in `Kick/modules/engine/audio_program/`:

- **Model class** `MT*Program : MTCommonProgram` (`source/mt_audio_drum_program.h/.cpp`):
  owns the ValueTree state, and in its ctor **creates the synth object,
  registers it with the `DawEngine`, wires output bus/sends/inserts and
  name/output callbacks** (`mt_audio_drum_program.cpp:28-60`). Forwards
  `noteOn/noteOff/…` (IMidiMsgProcessor) straight to the synth object
  (`mt_audio_drum_program.h:44-51`). Metering forwards too (`.h:40-41`).
- **Synth class** `*Program : ISynthObject` (`synth/`) as in §1.3.
- **Factory:** `MTAudioProgramManager::createProgram(ValueTree&, bool)`
  (`mt_audio_manager_program.cpp:98-122`) — a chain of
  `if (type == …) prog = new MT*Program(…)` for Drum/Keygroup/Clip/Midi/Cv/
  Audio. **No branch for Plugin (5) or anything else: an unlisted type leaves
  `prog == nullptr` and the program is silently skipped** — on load and on
  create. Non-fatal by construction.
- **Type ids:** `MTProgramType` (`mt_audio_interface_types.h:97-107`):
  Drum 0, Keygroup 1, Midi 2, Audio 3, Clip 4, **Plugin 5**, Cv 6, Unknown 7.
  `toString()` defaults to `"Unknown"` for unlisted values (`.h:236-249`);
  `fromString()` likewise (`.h:251-264`).
- **Load fan-out:** `MTAudioManager::loadProject` (`mt_audio_manager.cpp:153-207`)
  hands the `PROGRAMS` subtree to `MTAudioProgramManager::setup` (line 186),
  which calls `createProgram` per `PROGRAM` child
  (`mt_audio_manager_program.cpp:57-72`). Both KickAudioEngine and
  KickAudioModel host this manager over the shared engine modules, so one
  factory change serves both processes.
- **Note funnel:** everything — pads, sequences, external MIDI — reaches the
  program as `juce::MidiMessage`s via `MTIProgram::noteProcess`
  (`mt_audio_iprogram.cpp:261-…`), which dispatches to `noteOn/noteOff`
  according to the per-note `SamplePlayType`. **`MTCommonProgram`'s default is
  `ONE_SHOT`** (`mt_audio_common_program.h:71`) — a synth program must override
  it to `NOTE_ON`, or pad/key releases never release a voice.
- **Parameter persistence mechanism:** `Atomic<T>` wrappers
  (`Kick/modules/common/source/kick_atomics.h`) attach to ValueTree properties
  by `IDs::*` identifier with defaults and ranges, giving thread-safe
  audio-thread reads of document values. The established structs
  (`mt_audio_iprogram_atomics.h`): `MTIAtomicProgramMaster` (master
  gain/pan/semi/fine/poly, attaches to the PROGRAM tree itself, lines 27-73),
  `MTIAtomic
...[truncated 32767 chars]LFO` (lines 75-101), `MTIAtomicADSR` + Amp/Filter/Pitch envelope
  structs (lines 103-204) — the Filter envelope struct also carries the static
  filter settings (type, cutoff as **int Hz default 20000**, resonance,
  depth — lines 146-172).
- **Identifiers:** `IDs::*` are one-line `DECLARE_ID(name)` declarations
  (`mt_audio_ids.h:28`, 254 of them). PROGRAM/ENVELOPE/AMPENV/FILTERENV/
  PITCHENV/LFO and the property ids `attack/hold/decay/sustain/release/freq/
  resonance/depth/type/semi/fine/level/pan/pitch/filter/index/isEnabled/name/
  id` all already exist. A synth needs very few new ones (§6.2).
- **Build wiring:** the engine is JUCE modules; `audio_program` compiles by
  **unity inclusion** — `audio_program.cpp` `#include`s every synth/ and
  source/ .cpp explicitly (e.g. `synth/DrumProgram.cpp`,
  `source/mt_audio_drum_program.cpp`). New .cpp files must be added there.

### 1.5 What the GUI does with program types (blast radius of a new type)

- **Program lists / track selectors use names only**
  (`MTAudioProgramManager::updateProgramList`, `mt_audio_manager_program.cpp:232-244`)
  — any type displays and routes fine.
- **PROG EDIT is per-type:** `ProgramEdit::loadProgram` switches over exactly
  Cv/Midi/Clip/Drum/Keygroup/Audio (`kick_ProgramEdit.cpp:165-173`). An
  unhandled type leaves `selectedProgram == nullptr`, and the screen falls
  into its else branch: all editors hidden, info label shown
  (`kick_ProgramEdit.cpp:213-222`). `handleCommand` null-guards too
  (line 230). **Graceful — no crash, just no editor.**
- **The create-program menu** (`kick_AppMainMode.cpp:39`) has fixed buttons
  per type, including a Plugin button — a new type gets no button without GUI
  work (out of v1 scope; our programs are generated, §7).
- **Knob (qlink) path:** on the home screen, knob messages route to the
  knob-edit screen (`kick_AppHome.cpp:449-458`); inside PROG EDIT, the
  per-type editors' knob tabs map P1–P4 onto the *visible editor's*
  parameters. **There is no engine-level qlink→program-param bus.** Panel
  knobs reach program params only through an editor page.
- **Pad LEDs** are computed per program type in `kick_leds.cpp` (it has a
  Plugin case at line 404) — behaviour for an unlisted type was not fully
  traced (§11).

---

## 2. Playing surface — how notes reach a program

Every input converges on `MTTrack::processIncomingMidiMessage` →
`program->noteProcess` → `noteOn/noteOff`:

- **Pads (hardware, simulator, or web panel via the bridge):**
  `AppMainmode::handleCommand` (`kick_AppMainMode.cpp:392-420`) turns a pad
  press into `juce::MidiMessage::noteOn(1, padNo + bankOffset, velocity)` and
  a release into `noteOff`, sent to the selected track via
  `ae.addLiveMidiMsg`. `bankOffset = 16 × (bank − 'A')`
  (`mt_audio_interface.cpp:205-209`) — **banks A–H are 16-note chromatic
  windows spanning the full 0–127 range.**
- **Tracks** apply their effective-note/velocity mapping (transpose etc.) and
  forward to the track's program (`mt_audio_track.cpp:383-410`); sequence
  playback (the per-sequence `.mid` patterns) takes the same funnel with
  arbitrary note numbers (`mt_audio_track.cpp:296-377`).
- **External MIDI** enters tracks through the same function via
  `MTMidiMessage` + per-track input filtering (`mt_audio_track.cpp:412-417`;
  device handling in `Kick/modules/engine/audio_midi/`).

**Consequence for a melodic synth:** no new input plumbing is needed at all.
Pads give a playable 16-note chromatic window per bank (8 windows = full
range), gated noteOn/noteOff pairs exist end-to-end, sequences can play
chords/melodies across the whole range, and a MIDI keyboard is the
full-range surface. The only program-side requirement is the `NOTE_ON`
sample-play override (§1.4).

---

## 3. The Oberheim template — SEM voice → our implementation

Per Garth's direction, the voice architecture is the **Oberheim SEM**, the
reference point from his own project history: his MicroFreak work established
that instrument's filter as an Oberheim SEM-inspired 12 dB/oct state-variable
filter, with a modelled-analog-SEM-filter thread carried into his
MicroFreak-emulation goal; the classic SEM/OB voice is
VCOs → mixer → **2-pole multimode SVF** → VCA with one filter EG, one amp EG
and one LFO, replicated per voice in the OB polys. His DSP bars, from his own
code and lessons:

- **Filter = TPT/Zavalishin SVF** — his standing DSP lesson: "Naive
  Chamberlin SVF goes unstable when f > 1 … Use the TPT/Zavalishin SVF
  instead — unconditionally stable to Nyquist" (`~/AGENTS.md`, DSP lessons,
  Sep 29 2026). The TPT SVF is also the *correct* digital topology for a SEM
  SVF (same two-integrator state-variable structure).
- **Oscillators = PolyBLEP** — his house anti-aliasing: PolyBLEP residuals
  for saw/square in ElfSynth's engine (`~/workspace/elfsynth-core/crates/
  elfsynth-core/src/osc.rs:11-42`) and in the ElfPro800 plugin
  (`~/workspace/elfchords-p8/plugin/source/PluginProcessor.cpp:411-424`).
- **3 oscillators is his house layout** — ElfSynth One: 16-voice poly,
  "three morph oscillators + sub + noise" per voice
  (`~/workspace/elfsynth-core/crates/elfsynth-core/src/osc_section.rs:1-7`),
  with oscillator parameters in **plain musical units** — pitch in semitones
  −24..+24, fine in cents −100..+100, level 0..1 (`osc_section.rs:66-75`).
  His ElfPro800 panel feedback adds the presentation rule: controls show
  musical values (semitones/coarse-fine), not raw 0..1 — reflected in the
  schema in §6 (units are musical in the file; conversion happens in the
  voice, as `DrumVoice` does with its scalers).

### Block-by-block mapping

| SEM / OB block | Our implementation | Compose or write? |
|---|---|---|
| VCO 1, VCO 2 (saw + pulse, tune/detune) | **New small PolyBLEP oscillator** (saw / pulse with variable width / triangle / sine), modelled on the two house implementations above. `AudIO::LFO` was judged and rejected for the VCO role: nearest-neighbour tables as compiled, fixed 50 % square → **no PWM, and PWM is signature SEM**. (~120 lines, no deps.) | **Write** |
| VCO 3 (the ElfSynth expansion) | Same oscillator class, third instance. Proposed duties: default saw an octave down or detuned third voice; may double as an audio-rate mod source later (FM/ring-mod deferred, §8). It is *not* drafted as the LFO — the SEM LFO stays separate, as on the original. | Write (same class) |
| VCO mixer | Per-osc level (0..127 in schema → gain), summed into the filter input with a fixed headroom scale (3 oscs at full level will clip the SVF input; scale ≈ 1/√3 or a `drive`-less fixed 0.5 — tune in the render proof). | Write (trivial) |
| **Multimode SVF, 12 dB/oct** (SEM's variable LP→notch→HP + BP) | **`juce::dsp::StateVariableTPTFilter<float>` per voice** — a genuine TPT/Zavalishin 2-pole SVF (the same component the Kick voices already use via the `mt::Filter` wrapper, §1.3), cutoff in Hz, per-sample cutoff modulation for the filter EG/LFO. | **Compose** |
| — vs SEM resonance | JUCE SVF resonance is Q; at high Q it rings/self-oscillates more eagerly than a SEM (whose resonance is comparatively tame and never the point of the sound). Cap schema Q at ≈ 8 for SEM-ish behaviour. SEM's continuous mode morph (LP→notch→HP on one knob) is approximated by discrete LP/BP/HP modes; **notch = LP+HP sum** is available from the same filter if a 4th mode is wanted — v1 keeps the three stock modes to match `mt::FilterType`. | Note |
| — vs `AudIO::Filter` | Not an SVF, not usable (§1.1). The TPT requirement and the SEM topology land on the same component; there is no conflict to resolve. | — |
| VCA + amp EG | `mt::ADSR` (AudIO::ADSR<double>) #1, per-sample, exactly as `DrumVoice`'s ampEnv. | Compose |
| Filter EG | `mt::ADSR` #2 → cutoff multiplier (Hz × 2^(depth·env)), the SEM's env→filter amount = schema `depth`. | Compose |
| Pitch EG | `mt::ADSR` #3 with depth default 0 (the stock schema already has PITCHENV; SEMs route env to VCO pitch on some models — keep, cheap). | Compose |
| LFO (sine/tri/square/S&H, rate + delay) | `AudIO::LFO` in its intended role — vibrato (→pitch), →filter, →amp (tremolo), →pan, with the DrumVoice mod-routing pattern (`drum_voice.cpp:186-204`) as the wiring template. | Compose |
| Per-voice pan | `AudIO::Panner<float>` per voice (as DrumVoice). | Compose |
| OB poly character (bonus) | **Per-voice detune/pan spread**: OB polys sound wide because voices differ slightly. A schema `spread` (cents) applying a deterministic per-voice-index detune offset (±spread by voice slot) + a whisper of pan offset costs ~5 lines in `noteOn`. In schema from day one, default 0 (= off), implementation in v1 if the render proof stays green — flagged bonus, not a gate. | Write (trivial) |

**Net DSP verdict: we write exactly one real component (the PolyBLEP
oscillator) and compose everything else** from blocks the tree already
proves in production (TPT SVF, AHDSR, LFO, panner — all exercised by
`DrumVoice` today).

---

## 4. Integration strategy — the new program type

### 4.1 Type-id decision

| Option | For | Against | Verdict |
|---|---|---|---|
| **New id: `Synth = 8`** (add enumerator after `Unknown = 7`) | Self-documenting in files and UI strings; zero collision with upstream semantics; all audited consumers are default-safe (§1.5); `Unknown` stays the parse fallback. | GUI create-menu can't make one (v1 generates projects anyway); every type switch in the tree must be audited once (§11). | **RECOMMENDED** |
| Squat on `Plugin = 5` | GUI already has a Plugin create button (`kick_AppMainMode.cpp:39`), a "Plugin Program" knob-edit label, an LED case, toString/fromString entries. | The id is *reserved with intent* (external-plugin hosting) — upstream device software may implement it later, and ElfKick tracks upstream; our synth projects would then misload. Still needs the same factory branch + classes. Saving is one menu button we don't need in v1. | Rejected |
| Disguise as Keygroup (1) | No enum change. | Keygroup's `noteOn` is a stub (§1.3), its data model is per-note sample clips, and the GUI would open the keygroup editor on a sample-less program. Worst of both. | Rejected |

Concretely for `Synth = 8`:
- `mt_audio_interface_types.h`: add `Synth = 8` to `MTProgramType` (after
  `Unknown`, which keeps value 7), add `"Synth"` cases to `toString`/
  `fromString`.
- `mt_audio_manager_program.cpp:103-108`: add the factory branch
  `if (type == MTProgramType::Synth) prog = new MTSynthProgram(engine, VT, undoManager);`
- Everything else inherits audited default behaviour (§1.5, §11).

### 4.2 The classes (mirroring the Drum pair, file for file)

| New file | Mirrors | Role |
|---|---|---|
| `audio_program/source/mt_audio_synth_program.h/.cpp` — `MTSynthProgram : MTCommonProgram` | `mt_audio_drum_program.*` | Model side: ctor creates the synth object, `Engine.createObject`, `initAudioRouting()`, output bus + sends wiring, name/output/send callbacks, inserts setup from the tree's INSERTS child (`mt_audio_drum_program.cpp:28-60` pattern). Forwards noteOn/noteOff/meters to the synth object. **Overrides `getNoteSamplePlay()` → `SamplePlayType::NOTE_ON` for all notes** (the gating fix, §1.4). `iSampleCreator` stubs return `nullptr` (we have no samples; see §11 risk 3). |
| `audio_program/synth/synthProgram.h` + `SynthProgram.cpp` — `SynthProgram : ISynthObject` | `drumProgram.h` / `DrumProgram.cpp` | Audio side: ctor `ISynthObject(D, POLYPHONY::MAX)`, pre-creates 16 `SynthVoice`s. `setProgram()` attaches the shared synth atomics to the program tree's SYNTH child and hands them to the voices (alongside the `master` attach the base class already does, `iSynthObject.cpp:60-64`). `noteOn()` = allocate/retrigger/steal (§5.3); `noteOff` inherited. |
| `audio_program/synth/synth_voice.h/.cpp` — `SynthVoice : ISynthVoice` | `drum_voice.*` | The SEM voice (§5.1). Overrides `noteOn/noteOff/process`; never touches a slice. |
| `audio_program/source/mt_audio_synth_atomics.h` — `MTIAtomicSynthParams` | `mt_audio_iprogram_atomics.h` structs | Program-level parameter block (§6.2): 3 osc structs + synth globals, attached to the SYNTH subtree; reuses `MTIAtomicEnvelopes` + `MTIAtomicLFO` unchanged for ENVELOPE/LFO. |
| IDs in `mt_audio_ids.h` | — | New `DECLARE_ID`s: `SYNTH`, `OSC`, plus any of `wave`, `glide`, `spread`, `baseNote` not already defined (verify at implementation; most property ids exist). |
| `audio_program.cpp` | — | Add the three new .cpp files to the unity include list (§1.4). |

No changes to: the track/note funnel, the IPC/contracts, the GUI, the
simulator, the bridge, AudIO itself. KickAudioEngine + KickAudioModel pick
the instrument up automatically (shared modules), and the web panel /
KT_KickEM need nothing — a synth program is just a program.

---

## 5. Voice architecture

### 5.1 Signal flow (per voice, per sample — the DrumVoice loop shape)

```
note freq ──► osc1 (saw/pulse/tri/sine, semi/fine, level) ─┐
              osc2 (same, detuned)                          ├─► mix ─► TPT SVF ─► ×ampEnv ─► panner ─► tempBuffer ─► master gain L/R
              osc3 (same)                                   ┘        ▲  ▲
LFO ───────────────► pitch (vibrato) · filter · amp · pan ───────────┘  │
filterEnv ──────────────────────────────────────────► cutoff ×2^(depth·env)
pitchEnv ───────────► pitch (depth, default 0)
velocity ───────────► amp scaling (voice gain ramp, from ISynthVoice base)
```

Implementation notes, all following `DrumVoice::process`
(`drum_voice.cpp:180-212`):
- Per block: read atomic params once (they're atomics; DrumVoice re-reads
  per block via `update()`), compute base note frequency from
  `MidiMessage::getMidiNoteInHertz(note)` × master `pitchRatio`
  (program semi/fine, already maintained by `MTIAtomicProgramMaster`).
- Per sample: evaluate LFO + 3 envelopes (the stock pattern evaluates envs
  per sample), osc frequencies = base × osc semi/fine ratio × pitchEnv/LFO
  modulation × glide slew; filter cutoff = base Hz × 2^(filtEnv × depth +
  LFO × lfoToFilter), clamped to [20, sr×0.49]; `filter.processSampleStereo`
  or two mono instances; amp env × velocity gain; panner; accumulate into
  `tempBuffer`, then `Buffer.addFrom` with master gains — identical tail to
  DrumVoice.
- Voice end: when the amp envelope reaches IDLE after noteOff (and a short
  tail-off ramp like DrumVoice's `tailOff` to avoid clicks), `makeFree()`.
  `ISynthVoice::noteOff()` sets STOPPING; our `process` owns the
  env-release → free transition (DrumVoice does the same via
  `hasFinished()` on its renderer; ours keys on the amp ADSR state).
- **Sample rate / block size come from `prepareToPlay(samplerate,
  blocksize)`** (voice: `iSynthVoice.h:64`; object: `iSynthObject.cpp:175-180`)
  and are pushed into every DSP block (`setSampleRate`) at noteOn, exactly
  as `DrumVoice::noteOn` does (`drum_voice.cpp:33-39`). Nothing hardcodes
  48000/960 — those constants are our reconstruction, and the device may
  run otherwise.
- Glide: a `juce::SmoothedValue` on log-frequency, time = schema `glide`
  seconds; 0 = off. (SEM/OB portamento is part of the character; the
  mechanism is one smoothed value.)

### 5.2 The oscillator (the one written component)

Small, self-contained, header-style class in `synth_voice` files:
- Phase accumulator (double phase, float out), freq set per sample from the
  modulated frequency.
- Waveforms: **saw** (naive + PolyBLEP residual at the wrap), **pulse**
  (naive square at width w + BLEP at both edges; width from schema, LFO→
  width = PWM), **triangle** (integrated-square style or naive+BLEP —
  triangle aliases little; use the elfsynth-core approach), **sine**
  (std::sin or a small table — sine has no discontinuity; std::sin per
  sample ×3 osc ×16 voices is acceptable, table is a later optimisation).
- PolyBLEP residual formula copied in spirit from
  `elfsynth-core/src/osc.rs:16-42` / ElfPro800 `PluginProcessor.cpp:411-424`
  (both are the standard t/(1−t) polynomial BLEP).
- No allocations, no tables to initialise, no platform code.

### 5.3 Allocation, retrigger, stealing (subclass policy, base untouched)

`SynthProgram::noteOn(Note, Vel)`:
1. If a voice is already playing `Note` (`isPlayback`), **retrigger it**
   (reset oscs' phase? SEM retrigger keeps osc phase running — free-run —
   and restarts both EGs; do that: `voice->noteOn` re-gates envelopes,
   phase untouched) — one voice per note, always.
2. Else `getFreeVoice(Note, 1)`; on success, setup + `addVoiceForPlayback`.
3. Else (pool exhausted) **steal**: prefer a STOPPING voice, else the
   oldest RUNNING voice (the subclass keeps a monotonically increasing
   start-order stamp per voice — 16 stamps, trivial); hard-release it
   (fast 5 ms fade via the voice's gain ramp to avoid a click) and reuse.
This fixes, for our instrument only, the stock empty steal branch
(`iSynthObject.cpp:106-108`) without touching shared code.

Polyphony ceiling: pool = `POLYPHONY::MAX` = 16 (also the program `poly`
atomic's range). 16 voices × 3 oscs is a design choice matching both the
engine constant and ElfSynth One's 16 voices.

---

## 6. Parameters + persistence

### 6.1 Schema — the SYNTH subtree inside PROGRAM

Everything hangs off one new child of the PROGRAM tree, reusing the stock
ENVELOPE/LFO subtrees verbatim inside it (so `MTIAtomicEnvelopes` and
`MTIAtomicLFO` attach unchanged) plus an OSC list in the file's established
style (repeated children distinguished by an `index` property — the same
pattern as CLIP/`keyNote`):

```xml
<PROGRAM name="Elf Synth" type="8" id="ElfSynth1" OUT="Out-1,2" semi="36">
  <SYNTH glide="0.00" spread="0">
    <OSC index="0" isEnabled="1" wave="0" semi="0"   fine="0"  level="110" width="0"/>
    <OSC index="1" isEnabled="1" wave="0" semi="0"   fine="7"  level="90"  width="0"/>
    <OSC index="2" isEnabled="1" wave="1" semi="-12" fine="0"  level="70"  width="64"/>
    <ENVELOPE>
      <AMPENV    type="0" isEnabled="1" attack="0.005" hold="0" decay="0.25" sustain="0.8" release="0.30"/>
      <FILTERENV type="0" isEnabled="1" attack="0.010" hold="0" decay="0.40" sustain="0.4" release="0.30"
                 freq="8500" resonance="0.7" depth="48"/>
      <PITCHENV  type="0" isEnabled="0" attack="0.005" hold="0" decay="0.20" sustain="0" release="0.20" depth="0"/>
    </ENVELOPE>
    <LFO isEnabled="0" type="0" freq="5.0" pitch="0" filter="0" level="0" pan="0"/>
  </SYNTH>
</PROGRAM>
```

`semi="36"` is the *existing* program-master property (attaches on the
PROGRAM tree, `mt_audio_iprogram_atomics.h:44`): it transposes the whole
program, which is how the generated project places pad bank A (notes
0–15) at C3–D♯4 instead of sub-bass C0. No new mechanism.

### 6.2 Parameter table (musical units in the file — Garth's rule)

| Param | Where | Unit / range | Default | Notes |
|---|---|---|---|---|
| osc.isEnabled | OSC | bool | 1 (osc1), 1, 1 | |
| osc.wave | OSC | enum 0=Saw 1=Pulse 2=Triangle 3=Sine | 0 | SEM order (saw+pulse are the SEM's pair) |
| osc.semi | OSC | **semitones, int −24..+24** | 0 / 0 / −12 | matches `SEMI` conventions + ElfSynth OscParams |
| osc.fine | OSC | **cents, int −100..+100** | 0 / +7 / 0 | the +7 cents on osc 2 is the OB chorus-without-a-chorus trick |
| osc.level | OSC | int 0..127 (file convention) | 110/90/70 | → gain via `MIDI::SCALER` like every other level in the schema |
| osc.width | OSC | int 0..127 → pulse width 0.05..0.95 | 64 (≈50 %) | pulse only; LFO can modulate (PWM) via LFO `pitch`-slot reuse — no: PWM depth gets **no** v1 slot; width is static in v1, PWM deferred (§8) |
| filter.type | FILTERENV | enum 0=LP 1=HP 2=BP — **`mt::FilterType` order (LowPass/HighPass/BandPass)**, not the AudIO::Filter order | 0 | stored in FILTERENV beside cutoff, as the stock struct does |
| filter.freq (cutoff) | FILTERENV | **Hz, float 20..20000** | 8500 | stock FILTERENV stores cutoff as int Hz (default 20000); we store float Hz in our own atomics (§6.3) — the format is variant-typed, both persist |
| filter.resonance | FILTERENV | **Q, float 0.5..8** | 0.7 (≈ SEM resting reso) | §3 cap rationale. (Stock drum path's stored-int resonance semantics are ambiguous — §11; we define our own clearly.) |
| filter.depth (env amount) | FILTERENV | int −127..+127 → octaves = depth/16 (±8 oct) | 48 (+3 oct) | sign allows negative env sweeps |
| amp/filter/pitch ADSR | *ENV | **seconds, float** (A/H/D/R), sustain 0..1 float | musical starting points above | `AudIO::ADSR` stage times are seconds natively (`adsr.h:196-208`). Do **not** copy DrumVoice's sample-length scaling — that is a one-shot-drum idiom (`drum_voice.cpp:88-145`). |
| lfo.* | LFO | type enum (AudIO::LFOType order), freq Hz float, mod depths int 0..127 | off | `MTIAtomicLFO` attaches as-is; freq stored in Hz (its `freq` atomic is float) |
| synth.glide | SYNTH | seconds, float 0..2 | 0 | portamento (§5.1) |
| synth.spread | SYNTH | cents, int 0..50 | 0 | OB per-voice detune/pan spread (bonus, §3) |
| program master | PROGRAM | existing: gain/pan/semi/fine/poly/mute/solo | — | free from `MTIAtomicProgramMaster`; `poly` (1..16) caps active voices |

### 6.3 Atomics design

`MTIAtomicSynthParams` (new header in `source/`, pattern-copied from
`mt_audio_iprogram_atomics.h`):
- `struct Osc { Atomic<bool> isEnabled; Atomic<int> wave, semi, fine, level, width; } osc[3];`
  attached to the three OSC children by index lookup.
- `Atomic<float> glide; Atomic<int> spread;`
- Filter statics: reuse the FILTERENV child but read cutoff/resonance with
  our own `Atomic<float>` attachments on the same properties (the tree is
  the document; two readers of one property would fight — so the synth's
  FILTERENV atomics **replace**, not duplicate, the stock
  `MTIAtomicFilterEnvelope`'s filter fields; the ADSR part of that struct
  is reused via `MTIAtomicADSR` inheritance as-is).
- `SynthProgram::setProgram` attaches the whole block to
  `VT.getOrCreateChildWithName(IDs::SYNTH)` — missing subtree/properties
  fall back to the defaults above, which is exactly the forgiving-load
  behaviour PROJECT_FORMAT.md documented (and why a hand-authored file
  only needs the properties it cares about).
- The generated project (generator extension, §9 step 5) writes the full
  subtree explicitly anyway — explicit is the golden-reference style
  established by ElfTest01.

### 6.4 Editing story (honest)

- **v1: parameters are authored, not edited.** The generated project *is*
  the preset. Changing the sound = change the XML (or the generator's
  values) and reload the project. This matches how ElfTest01 already works.
- The GUI will show the program in lists, on the track, and in the mixer
  (name-based, §1.5); PROG EDIT shows "No Programs Available" — wrong
  words, right behaviour (nothing sample-oriented to break). A one-line
  label improvement ("Program Edit is not available for Synth Program")
  is a possible v1.1 nicety, mirroring the existing Audio-program label
  (`kick_ProgramEdit.cpp:216-217`).
- **P1–P4 do not reach synth params in v1** (§1.5: qlinks are mediated by
  editor pages; there is no engine-level path). The route to knobs is a
  small `program_edit_ui` page for the Synth type (its own step, §8) —
  the atomics make it easy later because an editor would write the same
  ValueTree properties the voice already reads.
- Live control that *does* exist in v1 for free: track/program master
  controls (level, pan, semi/fine, mute/solo) — they're program-master
  atomics the mixer and pad-performance paths already drive.

---

## 7. v1 scope

**In v1 (playable, persistent, proven):**
1. New program type `Synth` (id 8) + factory branch + type strings.
2. `MTSynthProgram` / `SynthProgram` / `SynthVoice` on the stock patterns,
   16-voice polyphony, retrigger-per-note, oldest-voice stealing.
3. Voice: 3 × PolyBLEP osc (saw/pulse-width/triangle/sine) → mixer → TPT
   SVF (LP/HP/BP, Hz cutoff, env + LFO modulation) → amp AHDSR; filter +
   pitch AHDSR; one LFO (pitch/filter/amp/pan); per-voice pan; velocity;
   glide; spread default-off.
4. Full parameter persistence via the SYNTH subtree (§6); defaults make
   partial files load.
5. Playable from pads (all 8 banks), sequences (.mid), and external MIDI,
   with correct gated release everywhere (NOTE_ON override).
6. Generated demo project **ElfSynth01** (generator extension): Track 1 =
   synth program (semi +36), plus a small .mid sequence demoing a chord
   progression so it plays hands-free.
7. Offline render proof (§9 step 2) + in-rig meter proof (§9 step 6) —
   both must pass before it's called working.

**Explicitly not v1:** GUI editor page + P1–P4 mapping; PWM depth/LFO→width;
osc sync/FM/ring-mod (ElfSynth has them; SEM purists first); notch mode;
drive/saturation; per-voice filter tracking amount control (key tracking
fixed at 0 in v1 — filter follows env/LFO only); MIDI CC learn; presets
beyond the generated project; any change to stock types' behaviour.

---

## 8. Deferred / future (recorded so v1 doesn't paint into corners)

- `program_edit_ui` Synth page (the atomics are already the editor's data
  model; the page is pure GUI work) → unlocks P1–P4 qlink editing.
- PWM (LFO→width), osc2 sync to osc1, osc3→osc1 FM — the SEM/ElfSynth
  expansions, in that order of value.
- Notch mode (LP+HP sum) → completes the SEM mode set.
- Key tracking for the filter (cutoff × note factor) — one multiply.
- The web panel's MIDI-in forwarding through the bridge into a track
  (bridge currently doesn't forward browser MIDI; the engine side already
  accepts external MIDI, §2).

## 9. Build plan (each step has its own gate)

**Step 0 — Type-id audit (recon, no code).** Grep every `switch`/comparison
on `MTProgramType` across `Kick/modules` + `Kick/apps`; classify each as
safe-by-default / needs-a-case / unknown. Known so far: factory (we add),
ProgramEdit (safe), interface strings (we add), `mt_audio_interface.cpp`
sample-op switches (Drum/Keygroup/Clip cases with silent no-op default —
safe for a sample-less type), `kick_leds.cpp` (has per-type cases incl.
Plugin — **its default for an unlisted type must be read, not assumed**),
`kick_AppAssignSample.cpp:439` (Plugin case exists; sample assignment on a
synth program should no-op). *Gate: the audit table is in the commit
message/PR notes; no unknowns left unexamined.*

**Step 1 — Oscillator + harness-first DSP.** Write the PolyBLEP oscillator
and the voice render core so `SynthVoice::process` compiles against the
real `ISynthVoice`. *Gate: it builds in the audio_program unity.*

**Step 2 — OFFLINE RENDER PROOF (the decisive DSP gate).** A console
harness — a small JUCE console target in the style of the existing app
targets (the Phase 1 AudIO console examples prove headless console
targets link and run on this VM; they render via a real-time device,
which is *not* deterministic enough — so this harness pumps blocks
manually instead of using a device):
- Build a PROGRAM ValueTree in code with the §6 SYNTH subtree (defaults +
  a saw patch), attach a real `MTIAtomicProgramMaster` +
  `MTIAtomicSynthParams`, construct voices directly (they need no device,
  no Design, no files — `ISynthVoice`'s slice is null, §11 risk 2 is
  retired or worked around here first).
- `prepareToPlay(48000, 960)`; `noteOn(69, 127)`; pump `process()` for
  1 s into a `juce::AudioBuffer`; `noteOff()`; pump the release; write
  `synth-a440.wav` via `juce::WavAudioFormat`.
- Assertions (in-harness or Python/wave module): sustain RMS > −20 dBFS;
  fundamental 440 Hz ±1 % (zero-crossing + FFT peak); saw signature
  (harmonic amplitudes decreasing ≈ 1/n); output ≈ silence 0.5 s after
  release ends; a 60/64/67 triad render shows FFT peaks at
  261.6/329.6/392.0 Hz; a cutoff=200 Hz render's spectral centroid is
  < ½ the cutoff=12000 Hz render's. *Gate: all measurements pass and the
  WAV is kept in the repo as evidence.* (This is also where the mixer
  headroom scale from §3 gets tuned — with numbers, not ears.)

**Step 3 — Program classes + factory + type registration.** `MTSynthProgram`,
`SynthProgram`, atomics header, IDs, unity includes, factory branch,
toString/fromString. *Gate: full app build (all six binaries) green;
standalone AudIO + KT_KickEM regression builds green.*

**Step 4 — Model-only load test.** Hand-author a `.kickPrj` with a type-8
program (no generator yet) in a scratch project dir; boot the rig
headless; the model must construct the program (log/list evidence via the
GUI project screen or the model's program list), with the Drum project
ElfTest01 still loading identically. *Gate: program list shows
"Elf Synth"; no crash on load/close/reload; project re-save (when
triggered) preserves the SYNTH subtree byte-for-byte in structure.*

**Step 5 — Generator + content.** Extend `scripts/make_test_project.py`
(or a sibling `make_synth_project.py`) to emit **ElfSynth01** per §7.6,
including the demo `.mid`. *Gate: generator output loads (step 4 checks)
and the file diffs clean against the intended schema.*

**Step 6 — In-rig end-to-end proof (the pad-gate method).** Full staged
rig; select the synth track; WS/panel **pad press-and-hold** (bank A) →
the synth program's render-fed output meters light and *sustain* while
held (unlike the drum one-shot decay — the distinguishing evidence of a
gated synth voice); release → meters fall after the release time.
Trigger the demo sequence → meters + (optionally) the streamed GUI show
chord playback. Switch to ElfTest01 and re-prove the drum gate
(non-regression). *Gate: meter evidence frames saved, both programs
verified in one session.*

**Step 7 — Docs + commit.** STATUS.md/ROADMAP.md Phase 5 updates,
BUILD_FINDINGS entry (new type + classes are an ElfKick addition, marked
as such), SYNTH subtree documented in PROJECT_FORMAT.md. Commit via the
Mac route; Garth pushes.

---

## 10. Device portability (same source → Buildroot/ARM build)

- All new code lives in the shared `audio_program` JUCE module and the
  `interface` ids header — compiled into every target (desktop apps now,
  the Buildroot image later) with **no new module dependencies**
  (`juce_dsp` is already linked by the apps that host the engine; the
  module list in `Kick/modules/engine/CMakeLists.txt` is untouched).
- **No x86 intrinsics, no SIMD intrinsics at all, no desktop APIs, no
  file I/O in the instrument** (oscillators are analytic — the synth
  needs no samples, which also sidesteps the whole stripped-content
  problem). Plain C++17 + JUCE, float audio path (double only in phase
  accumulation + envelope internals, mirroring existing code).
- **No allocations in `process()`**: voices pre-allocated in the
  `SynthProgram` ctor (stock pattern), temp buffers pre-sized, atomics
  are lock-free reads. Per-sample `std::sin` for the sine wave is the
  only transcendentals-heavy spot besides `pow` for ratios — ratios are
  computed per block/per note, not per sample (follow DrumVoice: pitch
  *ratio* per sample is a multiply).
- **Geometry from `prepareToPlay`, never from the reconstructed
  constants** (§5.1) — the design stays correct if the device runs a
  different rate/block size.
- CPU sanity: 16 voices × (3 osc + 1 SVF + 3 env + 1 LFO) per sample is
  well under the sampler's per-voice cost already shipping (slice
  resampling + pitch shifting); the render harness (step 2) should log
  single-voice render time as a soft check.
- License: AGPL headers on new files, matching the tree.

---

## 11. Risks + unknowns register (every one flagged, none smoothed over)

1. **BIGGEST RISK — un-audited `MTProgramType` consumers.** A new type id
   touches an enum the whole app switches on. The sampled consumers are
   safe (§1.5), but the full audit is Step 0 precisely because a missed
   one (LED colour computation in `kick_leds.cpp` is the known
   not-fully-traced case) could misbehave at runtime in ways the load
   test won't show (wrong pad colours = cosmetic; a null deref = crash).
   Mitigation: audit first, and the in-rig proof (§9.6) exercises pads,
   LEDs (via bridge feedback), mixer, and program switching in one
   session.
2. **`ASliceRenderer` with a null slice.** `ISynthVoice::noteOn` calls
   `ASliceRenderer::start()` (`iSynthVoice.cpp:44`); `DrumVoice` always
   has a slice. Whether `start()/stop()` are null-safe is **unverified**
   — the harness (Step 2) hits this first; worst case we set a silent
   dummy slice or replicate the base's tiny state/gain handling in our
   override. Not expected to block, must not be assumed away.
3. **`MTCommonProgram` sample machinery.** Our model class inherits
   per-note sample maps and must implement `iSampleCreator` (returning
   `nullptr`). Whether any generic flow (refresh, GUI sample assignment,
   duplicate-program) calls `getOrCreate()` on a sample-less program and
   then dereferences the result is **not fully traced** — Step 0/4
   checks; the factory's silent-skip precedent suggests the codebase
   tolerates nulls, but "suggests" is doing work in that sentence.
4. **Autosave round-trip of the SYNTH subtree.** The ValueTree *is* the
   document, so an unknown subtree should persist untouched; but the
   standing PROJECT_FORMAT follow-up (autosave didn't rewrite ElfTest01
   at all in the gate test) means persistence of *any* hand-authored
   content under the full app is not yet positively proven. Step 4
   verifies explicitly.
5. **Stock resonance units.** `MTIAtomicFilterEnvelope::resonance` is an
   int (default 10) that DrumVoice passes raw into a Q clamp of
   0.1–10 — the stored unit is ambiguous upstream. We sidestep it with
   our own float-Q atomics on the same child (§6.3), at the cost of one
   more place where our schema diverges from stock conventions.
6. **`mt::Filter`'s missing per-sample overload** (`drum_voice.cpp:200`
   vs `AudIO_juce.h`) — evidence the private AudIO_juce API is partly
   inferred. Doesn't block this design (we call `juce::dsp` directly),
   but any future "use the wrapper like DrumVoice does" refactor must
   reckon with it.
7. **Mixer headroom / SVF input level.** Three full-level oscillators
   into a resonant SVF will distort/clip without a deliberate input
   scale; the right number (and whether a touch of drive is *desirable*
   SEM character) is a Step-2 measurement, not a guess.
8. **What "type" the GUI header selector shows for id 8 before
   `toString` is updated** — cosmetic only ("Unknown" until Step 3 adds
   the case, which is part of Step 3, so this is a non-risk if steps
   are done in order; recorded for completeness).

---

## Appendix — key citations index

- DSP: `AudIO/AudIO/Dsp/lfo.h`, `lfo.cpp:47-76,99-100,117-141,168-169,180-224,245-264`;
  `Dsp/adsr.h:196-208,258`; `Dsp/filter.h:71,97,104-129`; `Includes/idsp.h:31-58`.
- AudIO objects: `Objects/SineWave.h:15-46`, `SineWave_adsr.h:13-41`,
  `SFSynth.h:23-70`; `Example/Synth/Main.cpp:44-68,212-220`.
- Wrappers: `AudIO/AudIO_juce/AudIO_juce.h:113-183` (RECONSTRUCTED module;
  API attested by upstream call sites).
- Voice framework: `audio_program/synth/iSynthObject.h:24-84`,
  `iSynthObject.cpp:95-112,175-212`; `iSynthVoice.h:26-73`, `iSynthVoice.cpp`;
  `drum_voice.cpp:33-39,88-145,180-212`; `DrumProgram.cpp:29-49`;
  `KeygroupProgram.cpp:58-67` (stub).
- Program layer: `mt_audio_manager_program.cpp:57-72,98-122,232-244`;
  `mt_audio_drum_program.cpp:28-60`; `mt_audio_iprogram.cpp:261-…`;
  `mt_audio_common_program.h:71`; `mt_audio_iprogram_atomics.h:27-204`;
  `mt_audio_ids.h:28`; `mt_audio_interface_types.h:97-107,236-264`;
  `mt_audio_manager.cpp:153-207`; `audio_program.cpp` (unity includes).
- Playing: `kick_AppMainMode.cpp:392-420`; `mt_audio_interface.cpp:205-209`;
  `mt_audio_track.cpp:296-417`; `kick_AppHome.cpp:449-458`;
  `kick_ProgramEdit.cpp:149-173,213-230`.
- Constants: `AudIO/AudIO_constants/AudIO_constants.h:66-86` (RECONSTRUCTED —
  design reads geometry from prepareToPlay instead).
- House precedents: `~/AGENTS.md` (DSP lessons, TPT SVF);
  `~/workspace/elfsynth-core/crates/elfsynth-core/src/osc.rs:11-42`,
  `osc_section.rs:1-7,66-75`;
  `~/workspace/elfchords-p8/plugin/source/PluginProcessor.cpp:411-424`.
