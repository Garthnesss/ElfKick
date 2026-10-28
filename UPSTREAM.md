# Upstream provenance

- Source: https://github.com/MusicTribeOpenSource/0700-AAM
- Imported commit: `27194180ce5fbab3776ebce53e2d4b016597364e` (main, Oct 28, 2026)
- Initial upstream release commit: `04ee49e4` "Initial public release of BE 0700-AAM (Kick)"
- License: AGPL-3.0-or-later (Kick, AudIO, ComProtocol); GPL-2.0 for the
  Buildroot tree (not vendored in this repo — fetch from upstream).

## What differs from upstream

Everything is itemised in BUILD_FINDINGS.md and, for AudIO, in
audio-linux-fixes.patch. In short:

- AudIO: case-sensitivity fixes (Linux FS), missing standard includes,
  unity-build assumptions repaired for per-TU compilation, SFSynth parameter
  API updated to the current Object API, dead `doProcess()` device calls
  fixed, `prepareToPlay` null-guard, `AUDIO_DAW_EXTENSIONS=1` for the
  standalone DAW library.
- Reconstructed files that the release references but does not contain
  (each is marked RECONSTRUCTED in its header):
  `AudIO/juce_modules.cmake`, `AudIO/AudIO_constants/`, `AudIO/AudIO_juce/`,
  `ComProtocol/PktMgr/PktMgr.h`.
- Kick: `apps/kickem/CMakeLists.txt` gained the `configure_file(config.h.in …)`
  step every other app already had.
- NOT vendored: `Kick/modules/3rdParty/juce_serialport` (upstream removed it
  as unlicensed; `scripts/fetch-deps.sh` clones cpr2323/juce_serialport and
  applies a small self-containment patch to its header), the Buildroot
  `Linux/` tree, JUCE itself (fetched by CPM at configure time).

## Removed-by-upstream content (still absent here)

Factory samples, the Myriad Pro font, the embedded E-mu GM SoundFont
(`AudIO/AudIO/Objects/SoundFontSynthesis/AWE32_1MB.h` is an empty stub —
SFSynth starts with no font; load your own .sf2), the prebuilt aarch64 SDK.
