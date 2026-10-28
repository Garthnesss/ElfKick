# KT_KickEM (simulator) build log — ElfKick

Date: Oct 28, 2026 · VM: Ubuntu, gcc 13, CMake 3.28, 2 cores
Tree: this repo (`~/workspace/elfkick`), configure per `scripts/build-simulator.sh`
plus `-DCPM_SOURCE_CACHE` pointing at the previously downloaded JUCE cache.

Baseline: previous scratch build failed with ~1,960 error lines, rooted in
(a) the empty AudIO_constants stub, (b) missing private AudIO_juce classes,
(c) AudIO feature config (extensions/recorder off), (d) kickem code written
against an older API, (e) juce_serialport having no Linux implementation.

## Changes (in order)

1. `AudIO/AudIO_constants/AudIO_constants.h` — RECONSTRUCTED the missing
   constants (global namespaces MIDI, MIDI_CH, DB, GAIN, PAN, POLYPHONY,
   UNKNOWN_VALUE, AUDIO_* geometry) from Kick call sites, in the style of
   Kick's own kick_Constants.h. Values are inference; see header comment.
2. `AudIO/AudIO/AudIO.h` — feature switches wrapped in #ifndef guards;
   defaults changed: AUDIO_DAW_EXTENSIONS (1), AUDIO_OBJECT_AUDIO_FILE_RECORDER
   (1) — the app build needs both; standalone CMake already forced extensions.
3. Configure in `build-kick/` with CPM_SOURCE_CACHE reuse: **succeeds**
   (only the known non-fatal libserial pkg-config probe).
4. `AudIO/AudIO_juce/AudIO_juce.h` — RECONSTRUCTED the private module:
   `mt::Recorder` alias of AudIO::AudioRecorder; `mt::SampleRecorder` and
   `mt::Looper` over AudIO::DRecorder with Kick-side names mapped
   (setEnable own flag, setGain→setVolume, startRecording→start, ...);
   `mt::AudioDevice` = IAudioDevice backed by juce::AudioDeviceManager,
   copying through the AudIO buffer, silent when no sound card exists.
5. First build (background, `-k`): first root error —
   `AudIO/Objects/Recorder.h`: `AudioFile` undeclared. The header only
   included 3rdparty/AudioFile.h when JUCE_VERSION was undefined, but it
   holds a `unique_ptr<AudioFile<float>>` member. Moved that include out of
   the guard (PlayBack.h already does it this way).

Build 1 finished at **361 error lines** (from ~1,960). Roots and fixes:
6. `AudIO/AudIO_juce/AudIO_juce.h` — JUCE 7.0.12 removed the legacy
   AudioIODeviceCallback; implemented `audioDeviceIOCallbackWithContext`.
   `Looper::isEmpty()` no longer calls the non-const getRecorderData().
7. `AudIO/AudIO_juce/AudIO_juce.h` — added RECONSTRUCTED `mt::ADSR`
   (AudIO::ADSR<double> + 5-value setParameters + operator()),
   `mt::FilterType`, and `mt::Filter` (AudIO::Filter<double> + freqRange +
   buffer process) for synth/drum_voice.
8. `Kick/modules/common/source/kick_Constants.h` — EMPTY namespace gained
   the missing `String` and `SampleBuffer` sentinels used by engine code.
9. `Kick/modules/engine/interface/source/mt_sample_editor_interface.h` —
   include juce_audio_utils (AudioThumbnail types used but never included).
10. `Kick/modules/input_events/input_events.cpp` — SIM_BUILD path repaired:
    include common (Factory), wrap simulator (id, data) events into
    CPktMgr::HWMsg_t for dispatchHardwareEvent (buttons=BUTTON/EN_BTN_STATUS,
    pads SIM_PAD_ID_BASE=1000 → PAD/EN_PAD_STATE, qlinks SIM_KNOB_ID_BASE=1100
    → KNOBS/EN_KNOB_QLINK), and convert HWMsg_t → (pktCmd, bytes) for
    SimulatorInterface::sendMessageToHW.
11. Simulator migration (`Kick/apps/kickem/simulator/`):
    - `mt::Kick::PadButtonState/PadColourIds/PadAction` →
      contracts::DeviceControlContracts types (aliased in kick_Simulator.h);
      `getPadColourFromId` reimplemented locally as a name-based colour map.
    - `mt::Kick::HwControlCommands` → BtnLedSequence values (BankA..H,
      Main→EN_MAINMODE, Prefs→EN_PREF, Undo→EN_BACK, centralKnob→EN_KNOB_*,
      transport EN_RECORD/EN_OVERDUB/EN_STOP/EN_PLAY/EN_PLAYSTART, ...);
      pads emit SIM_PAD_ID_BASE+pos, qlinks SIM_KNOB_ID_BASE+idx.
    - PAD_PRESSED/PAD_RELEASED → BtnState values; TOTAL_DRUM_PADS → 16;
      rsize_t → size_t; `sig::Signal` → `sigslot::signal`; SIGNALS kept.
    - Displays: JUCE 7 getPrimaryDisplay() pointer API.
    - AppHome → kick_ui::KickHome reference singleton (deletInstance dropped).
    - `makeVisibleAndAddListner` / `makeVisibleAddListnerAndAddRadioGroup`
      no longer exist on ui::KickButton/KickImageButton — expanded to
      addAndMakeVisible + addListener (+ setRadioGroupId).
    - Unqualified Button/Slider/Justification/MouseEvent/int8 qualified.
    - palacaze sigslot has no `.emit()` — signals are invoked via
      `operator()` (as the rest of the tree does); all kickem call sites
      converted.
12. `Kick/modules/common/source/kick_Constants.h` — include
    juce_audio_basics (EMPTY::SampleBuffer needs it; common only guarantees
    juce_gui_basics).
13. Builds 2–4: error count 361 → 26 → 58 (mid-edit snapshots) → 1, then
    **all TUs compile**. Remaining failures were at link:
    - `contracts::*` undefined (58 syms): the contracts module was never
      linked into kickem. `Kick/apps/kickem/CMakeLists.txt` now links
      `contracts` and `juce::juce_dsp` directly, like apps/kick and
      apps/kick_gui do (juce_dsp is needed by the reconstructed mt::Filter).
    - `SerialPort::close()` undefined: juce_serialport has no Linux
      implementation. Added repo-kept stub
      `Kick/modules/3rdParty/juce_serialport/juce_serialport_Linux.cpp`
      (fail-safe no-ops; .gitignore exception so it is committed, and
      fetch-deps preserves it), added to kickem's sources.
