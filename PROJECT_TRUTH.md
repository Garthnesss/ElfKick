# ElfKick — Project Truth

## One line
A maintained, buildable fork of Music Tribe's open-source **Behringer KICK**
(BE 0700-AAM) firmware — the $150-class MPC-style standalone sampler/groovebox
whose source was released 2026-10-26 under AGPL-3.0-or-later.

## What it is
- A **fork, not a clone-and-freeze**: upstream is
  [MusicTribeOpenSource/0700-AAM](https://github.com/MusicTribeOpenSource/0700-AAM)
  @ `2719418`. ElfKick tracks it, fixes what doesn't build, and documents
  everything it changes (BUILD_FINDINGS.md, audio-linux-fixes.patch,
  UPSTREAM.md).
- The upstream release is a **code dump that does not build as shipped** —
  its own README says the build "will not work out of the box." ElfKick's job
  is to turn it into something a person can actually compile, run, and hack on.
- Working target order: (1) the **AudIO engine** standalone on Linux,
  (2) the **kickem simulator** on a desktop, (3) the full multi-process app,
  (4) the Buildroot image for the real hardware (i.MX8M Mini), when hardware
  exists to test on.
- Everything learned about the upstream tree lives in this repo: the
  architecture (ARCHITECTURE.md), what's broken and why (BUILD_FINDINGS.md),
  and what state each piece is verified to be in (STATUS.md).

## What it is not
- Not a Music Tribe / Behringer product. "ElfKick" is the fork's name;
  Behringer/KICK names and logos are Music Tribe trademarks.
- Not a from-scratch groovebox. The instrument design, UI, and DSP are
  Music Tribe's; our work is build repair, reconstruction of omitted pieces,
  and (later) features on top.
- Not a promise about the hardware: price (~$150) and availability are
  unconfirmed upstream rumours. No ElfKick work depends on owning a unit yet.
- Not a vendoring of everything: the Buildroot `Linux/` tree and JUCE are
  fetched from upstream/their homes, not copied here (see UPSTREAM.md).

## Non-negotiables
1. **AGPL stays AGPL.** All upstream code keeps its license and attribution.
   Reconstructed replacement files are marked RECONSTRUCTED in their headers
   and listed in UPSTREAM.md. Nothing unlicensed is vendored — that's why
   `juce_serialport` is fetched by script, not copied in.
2. **Honest status, always.** BUILT NOT TESTED, RUNS, and CRASHES are
   different claims and are labelled as such in STATUS.md. A green build is
   never reported as a working program without a run.
3. **Fixes are minimal and explained.** Every change to upstream code is
   itemised in BUILD_FINDINGS.md with the reason (case-sensitivity, missing
   include, unity-build assumption, API drift). No silent rewrites.
4. **Pristine upstream stays pristine.** Work happens on the copies in this
   repo; the reference clone of upstream is never edited, so any fix can be
   re-derived as a clean diff (audio-linux-fixes.patch is generated that way).
5. **Verify on the machine you claim.** Linux results are from Linux runs;
   nothing is claimed for macOS/Windows/ARM hardware until it has run there.

## Confirmed by Garth
- Oct 28, 2026: fork it as **ElfKick**, public repo
  [Garthnesss/ElfKick](https://github.com/Garthnesss/ElfKick), maintained in
  git from `~/Projects/ElfKick` on his Mac.
- Oct 28, 2026: every project gets the four canonical docs —
  PROJECT_TRUTH, ARCHITECTURE, ROADMAP, STATUS — starting with this one.

## Scope boundaries
- No hardware purchases, no flashing, no Buildroot image work until a KICK
  unit exists and Garth says go.
- The "AI camera" is out of scope until code for it surfaces — the released
  tree contains no camera/gesture code at all (see ARCHITECTURE.md).
