/*
 * Copyright (c) 2026 ElfKick contributors
 *
 * This file is part of ElfKick (a fork of Music Tribe's Kick).
 *
 * ElfKick is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ElfKick is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with ElfKick. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// ElfKick addition (not upstream Kick code) — SYNTH_DESIGN.md §5.1.
//
// SynthVoice: one Oberheim/SEM-flavoured synth voice.
//   3x PolyBLEP oscillators -> mixer -> per-voice TPT SVF (the SEM's
//   12 dB state-variable filter; juce::dsp::StateVariableTPTFilter used
//   directly — AudIO::Filter is a one-pole cascade, not an SVF, and
//   mt::Filter has no per-sample cutoff modulation) -> amp AHDSR.
// Three stock mt::ADSR envelopes (amp/filter/pitch, times in seconds),
// one AudIO::LFO (vibrato/filter/tremolo/pan), glide as a smoothed
// log-frequency, deterministic per-voice spread detune/pan.
// The voice owns no sample slice: ISynthVoice's renderer calls are
// null-slice safe (ASliceRenderer::start/stop/reset guard on the slice),
// and voice lifetime here is keyed on the amp envelope reaching IDLE,
// never on hasFinished().

#pragma once

#include "iSynthVoice.h"
#include "polyblep_oscillator.h"
// MTIAtomicSynthParams arrives via audio_program.h (source/mt_audio_synth_atomics.h),
// mirroring how DrumVoice receives MTIAtomicLFO/MTIAtomicEnvelopes.

namespace mt
{
    class SynthVoice : public ISynthVoice
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<SynthVoice>;

        explicit SynthVoice( MTIAtomicProgramMaster& Master, int VoiceIndex )
            : ISynthVoice( Master ), voiceIndex( VoiceIndex ) {}
        virtual ~SynthVoice() = default; // ISynthVoice declares no virtual dtor (cf. DrumVoice)

        // Attach this voice's parameter atomics to the program's SYNTH
        // subtree (called by SynthProgram::setProgram).
        void attachSynth( const juce::ValueTree& SynthTree );

        void noteOn( int Number, int Velocity ) final;
        void noteOff() final;
        void prepareToPlay( double SampleRate, int Blocksize ) override;
        void process( juce::AudioBuffer<float>& Buffer ) override;

        // Voice-stealing support (SynthProgram, design §5.3).
        bool     isReleasing() const { return releasing; }
        void     forceQuickRelease(); // 5 ms amp release, gates off now
        uint64_t startStamp{ 0 };     // monotonic note-on order, set by SynthProgram

    private:
        void updateEnvelopes();
        void updateLFO();

        MTIAtomicSynthParams params;
        MTIAtomicLFO         pLfo;

        PolyBlepOscillator osc[3];
        juce::dsp::StateVariableTPTFilter<float> filter;
        int cachedFilterType{ -1 };

        AudIO::LFO           lfo;
        AudIO::Panner<float> panner;
        ADSR                 ampEnv;
        ADSR                 filtEnv;
        ADSR                 pitchEnv;

        juce::SmoothedValue<double> glideLogFreq{ 0.0 }; // log2 Hz
        bool hasGlideTarget{ false };
        bool releasing{ false };
        bool quickRelease{ false }; // steal path: amp release forced to 5 ms

        float lfoToAmp{ 0 };
        float lfoToPan{ 0 };
        float lfoToPitch{ 0 };  // semitones at full depth
        float lfoToFilter{ 0 }; // octaves at full depth

        int voiceIndex{ 0 };
    };

} // namespace mt
