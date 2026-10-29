# ElfKick — Kick project / program file format (recon, Oct 28, 2026)

Reverse-engineered from the loader code in this tree. Every claim cites
file:line. Read-only recon — no code was changed. Bottom line up front:
**a project is a directory containing one plain-text XML file
(JUCE `ValueTree` serialisation), and a minimal content-bearing project
can be authored with a small generator script today.**

---

## 1. A project on disk

A project is a **directory** named after the project, containing:

```
<Name>/
  <Name>.kickPrj     <- the project file (XML text, see §2)
  <Name>.png         <- home-screen tile artwork (optional in code, present in all shipped projects)
  Samples/  Export/  <- subdirs created by the app's own new-project flow
```

- The file extension and versions are constants:
  `__projectFileExtension = ".kickPrj"`, `__projectFileVersion = "0.1"`,
  `__projectVersion = "1.0.0"` — `Kick/modules/engine/project/project.h:41-43`.
- The loader derives the file path from the **directory name**:
  `xmlFile = <dir>/<dirname> + extn` —
  `Kick/modules/engine/project/kick_xml_manager.cpp:66-68`. So the file
  name must match its folder name exactly.
- Content roots on Linux (`Kick/modules/common/source/kick_paths.cpp`,
  Linux branch): demos `/app/demos/`, templates `/app/templates/`,
  user projects `/home/Kick/projects/`, samples `/data/sounds`,
  assets `/app/assets`.
- The app's own "new project" action creates only the folder skeleton
  (`Samples/`, `Export/`) and lets the loader create the file:
  `KickProjectsDefault::createProject` —
  `Kick/modules/engine/project/kick_settings.cpp:159-165`.
- Opening a demo/template **copies only the `.kickPrj`** into
  `/home/Kick/projects/<name>/` and loads it from there:
  `RecentProject::copyProjIntoProjectDir` —
  `Kick/modules/kick_ui/recent_project/kick_RecentProject.cpp:155-169`.
  Authored projects should therefore be written **directly into
  `/home/Kick/projects/<Name>/`** — no copying step needed.

### What the shipped demos actually contain

All 5 demos and all 5 templates in `Kick/demos/`, `Kick/templates/` ship
a **266-byte, byte-identical** `.kickPrj` (same md5 for all 10) — a stub
with a root element, an empty bus section and an empty file manager,
and **no programs, no sequences, no Project subtree**:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ROOT name="gg" manufacturer="Musictribe" version="0.1" bpm="120.0"
      length="4" num="4" den="4" isMasterBPM="1">
  <BUSES/>
  <FILEMANAGER id="AMowy9q0">
    <ASSETS/>
    <AUDIO/>
    <EXPORTS/>
  </FILEMANAGER>
</ROOT>
```

That stub — not missing artwork — is why "Demo 01" opens as an empty
project (§5). There is **no other `.kickPrj` anywhere in the tree**, and
no example program/preset file in `_playback` or the AudIO examples to
seed from; the skeleton in §6 is derived entirely from the loader code.

## 2. Format and load/save mechanics

- The `.kickPrj` is a **JUCE `ValueTree` serialised as XML text**:
  load is `juce::ValueTree::fromXml( xmlFile.loadFileAsString() )`,
  save is `vt.toXmlString()` written back with `replaceWithText` —
  `kick_xml_manager.cpp:82` (load), `:90-101` (save). Element names are
  the `juce::Identifier`s declared in
  `Kick/modules/engine/interface/source/mt_audio_ids.h:26-…`
  (`ROOT`, `PROGRAMS`, `PROGRAM`, `SEQUENCES`, `SEQUENCE`, `TRACK`,
  `CLIPS`, `CLIP`, `LAYERS`, `LAYER`, `FILEMANAGER`, `FILE`, …);
  properties are XML attributes.
- Load/save owner: `mt::projectManager : public KickXML`
  (`Kick/modules/engine/project/project.h:45-57`), constructed with root
  id `IDs::ROOT`, extension `.kickPrj`, file version `"0.1"`
  (`project.cpp:39-41`). The audio model drives it from
  `MTAudioManager::loadProject` —
  `Kick/modules/engine/audio_engine/engine/mt_audio_manager.cpp:159`.
- **Fallback on missing/empty file** (`KickXML::loadOrCreate`,
  `kick_xml_manager.cpp:57-87`): missing file → created empty; size 0 →
  a bare tree `{ROOT, manufacturer="Musictribe", version="0.1"}` is
  created and saved; then whatever XML is on disk is parsed.
- **No validation**: there is no checksum, no schema check, and the
  version strings are **never compared on load** (the only use of
  `__projectFileVersion` in the tree is the constructor call above).
  Each consumer reads only its own subtree and creates it empty if
  absent (§5), so unknown/missing elements are silently defaulted.
- **Autosave**: any change to the tree re-saves the file after 1.5 s
  (`kick_xml_manager.cpp:38-55`; `autoSave` defaults true). Corollary:
  ids the code auto-generates on load (§3) get written back into your
  file — author them explicitly if you want a stable file.
- IDs in this system are 8-char alphanumeric strings from
  `CommonUtils::createID()` —
  `Kick/modules/common/source/kick_common_utils.cpp:3-13`
  (the stub's `FILEMANAGER id="AMowy9q0"` is one). Any unique string
  works; nothing parses the format.

## 3. Structure: ROOT → programs / sequences / tracks

`MTAudioManager::loadProject` fans the ROOT tree out to the managers,
auto-creating each top-level subtree if missing
(`mt_audio_manager.cpp:165-195`): `BUSES` (bus manager), `FILEMANAGER`
(asset manager, `:172`), `TEMPOS` (tempo manager), `PROGRAMS` (program
manager, `:181`), `SONGS` (`:187`), `SEQUENCES` (sequence manager,
`:190`). ROOT's own attributes in the shipped stub (`bpm`, `length`,
`num`, `den`, `isMasterBPM`) are legacy decoration — the live tempo
state comes from the `TEMPOS` subtrees, which default when absent.

### Programs — `ROOT > PROGRAMS > PROGRAM`

- The program manager instantiates one program object per `PROGRAM`
  child; an empty `PROGRAMS` means **zero programs**
  (`Kick/modules/engine/audio_engine/engine/mt_audio_manager_program.cpp:60-68`).
- Program class is chosen from the **`type` attribute as an int**
  (`mt_audio_manager_program.cpp:101-118`):
  `0=Drum, 1=Keygroup, 2=Midi, 3=Audio, 4=Clip, 5=Plugin, 6=Cv`
  (`MTProgramType`, `Kick/modules/engine/interface/source/mt_audio_interface_types.h:97-107`).
  A missing `type` reads as 0 → Drum by accident; always write it.
- Everything else about a program is a `CachedValue` with a default, so
  it can be omitted (`MTIProgramData` ctor,
  `Kick/modules/engine/audio_engine/engine/mt_audio_interface_types.cpp:393-425`):
  `name` (""), `id` ("" — see below), `OUT` ("Out-1,2"), `mute`/`solo`
  (false), `level` (MIDI default 100), `semi`/`fine`/`transpose` (0),
  `poly` (16), `pan` (0). `COLOURS` and `PERFORMANCE` subtrees and the
  `SEND` gains subtree are auto-created with defaults by the same ctor.
- **`id` matters**: if empty, the program generates a random id on load
  and writes it back (`Kick/modules/engine/audio_program/source/mt_audio_iprogram.cpp:39-40`).
  Tracks reference programs **by this id** (§3, tracks), so an authored
  file must set `PROGRAM id="…"` explicitly and reuse the same string
  in the track.
- When the app itself creates a program it writes exactly
  `createVTree(IDs::PROGRAM, IDs::name, Name, IDs::type, toInt(Type))`
  (`mt_audio_manager_program.cpp:128-135`) — proof that
  `name` + `type` (+ our explicit `id`) is a complete program header.

### Drum program content — `PROGRAM > CLIPS > CLIP > LAYERS > LAYER`

A Drum program (`MTDrumProgram`) builds an AudIO `DrumProgram` synth
object in the DawEngine and routes it to the bus named by `OUT`
(`Kick/modules/engine/audio_program/source/mt_audio_drum_program.cpp:31-59`).
Its per-pad sounds ("clips") live under the program:

- Sample clips are the children of a `CLIPS` subtree
  (`MTIProgram` ctor: `iSampleVTree = vTree.getOrCreateChildWithName(IDs::CLIPS)`,
  `mt_audio_iprogram.cpp:46`); on load the map is rebuilt keyed by each
  clip's **`keyNote`** (`rebuildSampleMap`, `mt_audio_iprogram.cpp:83-93`;
  for drums only children of type `CLIP` are accepted,
  `mt_audio_drum_program.cpp:82-85`).
- `CLIP` attributes (all defaulted, `MTISample` ctor,
  `mt_audio_interface_types.cpp:109-147`): `keyNote` (60), `minNote` (0),
  `maxNote` (127), `gain` (1.0), `pan` (0), `semi`/`fine` (0), `mute`/
  `solo` (false), `muteGroup`, `poly`. A clip created by the app for a
  pad sets min=max=key=note (`mt_audio_drum_program.cpp:78-85`).
- Each `CLIP` holds a `LAYERS` subtree; the loader **auto-pads it to
  exactly 4 `LAYER` children** if fewer exist
  (`mt_audio_interface_types.cpp:148-162`; `MAX_LAYERS = 4`,
  `mt_audio_interface_types.h:52`).
- `LAYER` attributes: **`source`** (string, default "") and `slice`
  (int, default 0), plus `velocityStart` (0) / `velocityEnd` (127)
  (`MTISource`/`MTILayer` ctors, `mt_audio_interface_types.cpp:68-107`).
- **Trigger condition**: `DrumProgram::noteOn(note)` looks the note up
  by exact `keyNote`, and only plays if layer 0's `source` is non-empty
  and resolves to a loaded sample slice
  (`Kick/modules/engine/audio_program/synth/DrumProgram.cpp:58-128`).
  The voice that plays is an AudIO `DrumVoice` (sample playback through
  per-voice LFO/filter/amp envelopes — `synth/drum_voice.cpp`). There
  is **no oscillator drum-synth path**: a drum program with no sample
  source is silent by construction. The "synth voices" for our first
  content therefore = AudIO drum voices playing **a .wav we generate
  ourselves** (e.g. a pitch-swept sine kick).

### Sample assets — `ROOT > FILEMANAGER > ASSETS > FILE`

- On load, the asset manager restores every `FILE` child of `ASSETS`
  (samples), `AUDIO`, `EXPORTS`
  (`Kick/modules/engine/audio_file_manager/source/mt_audio_asset_manager.cpp:59-88`).
- A `FILE` node needs one attribute: **`path`**
  (`restoreAsset`, `mt_audio_asset_manager.cpp:119-145`). The path
  string is used verbatim as a `juce::File` and as the lookup key for
  slices; a `FILE` with no `SLICE` children gets a default slice 0
  automatically (`:130-134`).
- A layer resolves its sound via `getAssetSlice(source, slice)`
  (`mt_audio_asset_manager.cpp:324-328`, called from
  `mt_audio_interface_types.cpp:68-84`). **The `LAYER source` string
  must be character-identical to the `FILE path` string.** Use an
  absolute path in both places (e.g.
  `/home/Kick/projects/<Name>/kick.wav`) — that is unambiguous under
  both uses. Samples-type assets are cached at up to 5 s length
  (`mt_audio_asset_manager.cpp:127`), so keep authored wavs short.
- Buses: `<BUSES/>` may stay empty — bus definitions come from the
  DawEngine's own bus map; the tree only carries per-bus settings
  (`Kick/modules/engine/audio_engine/engine/mt_audio_bus_manager.cpp:156-177`),
  and the program default output name `"Out-1,2"` resolves against it.

### Sequences & tracks — `ROOT > SEQUENCES > SEQUENCE > TRACK`

- The sequence manager instantiates one sequence per `SEQUENCE` child
  (`mt_audio_manager_sequence.cpp:53-73`); `SEQUENCE` attributes all
  default (`name` auto "Sequence N", `length` 4, `isMasterBPM` true —
  `mt_audio_interface_types.cpp:461-486`).
- Tracks are **direct `TRACK` children of `SEQUENCE`** (no wrapper
  element) — `Kick/modules/engine/audio_engine/engine/mt_audio_sequence.cpp:60-68`.
- `TRACK` attributes (all defaulted, `MTITrackData` ctor,
  `mt_audio_interface_types.cpp:488-516`): `name`, `id`, `level`, `pan`,
  `mute`, `solo`, … and **`program`** — the id string of the program
  this track plays. It is resolved live against the program list
  (`mt_audio_interface_types.cpp:490-492` →
  `MTAudioInterface::getProgramDataForProgramID`,
  `Kick/modules/engine/audio_engine/engine/mt_audio_interface.cpp:576`).
  A track whose `program` matches no program id plays nothing.
- **Patterns are not in the XML.** Loading a sequence looks for
  `<projectDir>/<seqName>/<seqName>.mid` and creates it empty if
  missing (`mt_audio_sequence.cpp:49-58`) — step-sequencer notes live
  in that per-sequence Standard MIDI File (`NOTE` trees in the XML are
  only an editing-time view). Live pad playing does not need the .mid;
  it is auto-created on first load.

### Pad → sound chain (what the authored file must satisfy)

Pad press → (DCL/IPC, the input-path workstream) → note delivered to
the **selected track's program** → `DrumProgram::noteOn(note)` → clip
with that exact `keyNote` → layer with non-empty `source` → asset slice
→ `DrumVoice`.

> **Erratum (Oct 29, 2026, PAD_GATE_LOG Round 1 — supersedes the
> base-36 claim originally made here):** the physical pad-note space
> is **0-based**. `AppMainmode::handleCommand` builds
> `noteOn(1, padNo + bankOffset, velocity)` with
> `bankOffset = 16 × (bank − 'A')` (`getBankOffset`), and no stage
> after it transposes (track/sequence transposes default 0;
> `DrumProgram::noteOn` looks the note up verbatim). So bank A pads
> deliver notes **0–15**, and a smoke-test project's clips must be
> keyed **0–15** — as the generator now does. The
> `MTIPadPerformanceData` defaults (CHROMATIC, root C, octave "-2" →
> base 36) govern a different path (pad-performance/MIDI-input
> mapping), not physical pad delivery. §6's skeleton shows 36–51;
> read it as 0–15.

## 4. The empty-project fallback, end to end

Why Demo 01 opened empty — each step is a default, none is an error:

1. The shipped `.kickPrj` stub has no `Project`, `PROGRAMS` or
   `SEQUENCES` subtree (§1).
2. `projectManager::loadOrCreate` finds no `Project` child and appends
   an empty `<Project version="1.0.0"/>`
   (`Kick/modules/engine/project/project.cpp:64-78`).
3. `MTAudioManager::loadProject` auto-creates empty `PROGRAMS`,
   `SEQUENCES`, `SONGS`, `BUSES`, `TEMPOS` subtrees
   (`mt_audio_manager.cpp:165-195`).
4. Program/sequence managers iterate their children and find none
   (`mt_audio_manager_program.cpp:60-68`,
   `mt_audio_manager_sequence.cpp:59-65`) → **0 programs, 0 sequences,
   0 tracks**. The GUI is showing a faithfully-loaded empty project.

The same mechanism is the opportunity: anything we *do* put in those
subtrees is instantiated by the same code paths, with every omitted
attribute defaulted.

## 5. Feasibility verdict — recommended route

| Route | Verdict |
|---|---|
| Hand-author the XML | **Feasible** — plain text, no checksum/version gate, aggressive defaulting. But the full kit (16 pad clips × layers, id/path strings repeated in 3 places) is error-prone by hand. |
| **Generator script** | **Recommended.** A ~100-line Python script emits the exact XML below, synthesises the .wav(s), and drops the folder into `/home/Kick/projects/`. Deterministic, repeatable, extends to a full kit. |
| Drive the GUI to create it | Not viable yet: creating/assigning content through the UI depends on the pad/panel input path that is still being fixed, and on file-browser flows with no factory content to start from. Revisit as a *verification* route later (open the generated project, save from the app, diff). |

### Concrete steps for the first content-bearing project

1. Generator writes `/home/Kick/projects/ElfTest01/` containing
   `ElfTest01.kickPrj` (§6), `ElfTest01.png` (copy any demo tile), and
   `kick.wav` — a generated ~0.4 s pitch-swept sine (150→45 Hz) with an
   exponential decay, 48 kHz / 16-bit (the engine's reconstructed
   sample rate; any standard wav loads).
2. Boot the staged system, open **ElfTest01** from the Projects screen
   (projects dir is scanned by the home screen), confirm the program
   "Elf Drums" and track appear.
3. Press pads (bank A): with the input fix in, any pad 36–51 triggers
   the clip; evidence on this sound-card-less VM = engine/model log or
   meter movement, per the pad-test protocol.
4. Round-trip check: let the app autosave, re-read the `.kickPrj` —
   the loader will have normalised it (auto-created `COLOURS`,
   `PERFORMANCE`, `SEND`, extra `LAYER`s, `TEMPOS`, …). That saved
   file becomes the golden reference for the generator.

## 6. Minimal project skeleton (annotated)

Only the elements/attributes that carry information are shown;
everything omitted is defaulted or auto-created by the citations in §3.
`keyNote` 36–51 = bank A pads; repeat the `CLIP` block per note (the
generator loops). All 16 clips may point at the same wav for the smoke
test.

```xml
<?xml version="1.0" encoding="UTF-8"?>

<ROOT name="ElfTest01" manufacturer="Musictribe" version="0.1"
      bpm="120.0" length="4" num="4" den="4" isMasterBPM="1">
  <BUSES/>
  <FILEMANAGER id="ElfFM001">
    <ASSETS>
      <!-- path is used verbatim as the file location AND as the
           slice-lookup key; LAYER source below must match it exactly -->
      <FILE id="ElfFile1" path="/home/Kick/projects/ElfTest01/kick.wav"/>
    </ASSETS>
    <AUDIO/>
    <EXPORTS/>
  </FILEMANAGER>

  <PROGRAMS>
    <!-- type="0" = Drum. id is authored (not left to the random
         generator) because the TRACK references it. OUT defaults to
         "Out-1,2"; written here for clarity. -->
    <PROGRAM name="Elf Drums" type="0" id="ElfProg1" OUT="Out-1,2">
      <CLIPS>
        <!-- one CLIP per pad note; min=max=key mirrors how the app
             itself creates pad clips -->
        <CLIP keyNote="36" minNote="36" maxNote="36">
          <LAYERS>
            <!-- 1 LAYER is enough; the loader pads LAYERS to 4.
                 source MUST equal the FILE path above, verbatim. -->
            <LAYER source="/home/Kick/projects/ElfTest01/kick.wav" slice="0"/>
          </LAYERS>
        </CLIP>
        <!-- … identical CLIP blocks for keyNote 37 … 51 … -->
      </CLIPS>
    </PROGRAM>
  </PROGRAMS>

  <SEQUENCES>
    <SEQUENCE name="Sequence 01" length="4">
      <!-- TRACK is a direct child of SEQUENCE. program= must equal
           the PROGRAM id above. -->
      <TRACK name="Drums" program="ElfProg1"/>
    </SEQUENCE>
  </SEQUENCES>
</ROOT>
```

Deliberately omitted (all auto-created/defaulted on load):
`Project` subtree, `TEMPOS`, `SONGS`, `COLOURS`, `PERFORMANCE`, `SEND`,
`INSERTS`, `SPLAY`/`SMUTE`, `SLICE` children under `FILE`, the
sequence's `.mid` pattern file, and every `CachedValue` attribute not
listed above.
