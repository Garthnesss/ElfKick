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
// Modelled on DrumVoice's structure (per-note setup in noteOn, per-block
// parameter refresh in process, master gains applied on addFrom), with
// the differences the design mandates: oscillator synthesis instead of
// slice playback, envelope times used directly in seconds (no
// sample-length scaling), juce TPT SVF with per-sample cutoff
// modulation, and envelope-keyed voice lifetime.

#pragma once

#include "synth_voice.h"

namespace mt
{
    namespace
    {
        // Mixer headroom (design §5.1/§10): three full-scale oscillators
        // at schema levels sum to at most (110+90+70)/127 ~= 2.13.
        // Started at 0.5; the Step 2 render proof measured sustain RMS
        // -20.25 dBFS against the design's -20 dBFS floor, so it is set
        // to the design's other candidate, 1/sqrt(3) ~= 0.577 (+1.25 dB;
        // worst-case coherent peak ~1.23 pre-filter, full-patch triad
        // render peaks ~1.13 — the program master gain is the trim).
        constexpr double kMixScale = 0.5773502691896258; // 1/sqrt(3)

        juce::dsp::StateVariableTPTFilterType filterTypeFromInt( int Type )
        {
            switch( Type )
            {
                case 1: return juce::dsp::StateVariableTPTFilterType::highpass;
                case 2: return juce::dsp::StateVariableTPTFilterType::bandpass;
                default: return juce::dsp::StateVariableTPTFilterType::lowpass;
            }
        }
    } // namespace

    void SynthVoice::attachSynth( const juce::ValueTree& SynthTree )
    {
        params.attachTo( SynthTree );
        pLfo.attachTo( SynthTree.getChildWithName( IDs::LFO ) );
    }

    void SynthVoice::prepareToPlay( double SampleRate, int Blocksize )
    {
        ISynthVoice::prepareToPlay( SampleRate, Blocksize );

        for( auto& o : osc )
            o.prepare( SampleRate );

        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = SampleRate;
        spec.maximumBlockSize = (juce::uint32)juce::jmax( 1, Blocksize );
        spec.numChannels      = 1;
        filter.prepare( spec );
        filter.setType( filterTypeFromInt( params.filterType() ) );
        filter.setResonance( juce::jlimit( 0.1f, 12.0f, (float)params.resonanceQ() ) );
        filter.reset();
        cachedFilterType = params.filterType();

        ampEnv.setSampleRate( SampleRate );
        filtEnv.setSampleRate( SampleRate );
        pitchEnv.setSampleRate( SampleRate );
        lfo.setSampleRate( SampleRate );

        glideLogFreq.reset( SampleRate, 0.05 );
    }

    void SynthVoice::updateEnvelopes()
    {
        // Stage times are float SECONDS in the schema (AudIO::ADSR's
        // native units) — deliberately NOT DrumVoice's sample-length
        // scaling, which is a one-shot drum idiom (design §6.2).
        const double r = quickRelease ? 0.005 : (double)params.ampEnv.r();
        ampEnv.setParameters( params.ampEnv.a(), params.ampEnv.h(), params.ampEnv.d(), r, params.ampEnv.s() );
        filtEnv.setParameters( params.filterEnv.a(), params.filterEnv.h(), params.filterEnv.d(), params.filterEnv.r(), params.filterEnv.s() );
        pitchEnv.setParameters( params.pitchEnv.a(), params.pitchEnv.h(), params.pitchEnv.d(), params.pitchEnv.r(), params.pitchEnv.s() );
    }

    void SynthVoice::updateLFO()
    {
        lfo.setType( toEnum<LFOType>( (int)pLfo.type() ) );
        lfo.setFrequency( pLfo.freq() );
        lfo.setEnabled( pLfo.isEnabled() );

        lfoToAmp    = pLfo.ampMod() * MIDI::SCALER;
        lfoToPan    = pLfo.panMod() * MIDI::SCALER;
        lfoToPitch  = pLfo.pitchMod() * MIDI::SCALER;   // x1 semitone max (vibrato range)
        lfoToFilter = pLfo.filterMod() * MIDI::SCALER * 3.0f; // x3 octaves max
    }

    void SynthVoice::noteOn( int Number, int Velocity )
    {
        const bool wasFree = isFree();

        // Sample-rate dependent setup (DrumVoice pattern).
        lfo.setSampleRate( sampleRate );
        ampEnv.setSampleRate( sampleRate );
        filtEnv.setSampleRate( sampleRate );
        pitchEnv.setSampleRate( sampleRate );
        for( auto& o : osc )
            o.prepare( sampleRate );

        updateEnvelopes();
        updateLFO();

        cachedFilterType = params.filterType();
        filter.setType( filterTypeFromInt( cachedFilterType ) );
        filter.reset();

        // Glide: smoothed log2 frequency. Program-master transposition
        // (semi/fine via master.pitchRatio) is baked into the target;
        // osc-level semi/fine apply per sample in process().
        const double noteHz    = juce::MidiMessage::getMidiNoteInHertz( Number ) * (double)master.pitchRatio.load();
        const double logTarget = std::log2( juce::jmax( 1.0, noteHz ) );
        const float  glideSec  = params.glide();
        if( glideSec <= 0.0f || wasFree || !hasGlideTarget )
        {
            glideLogFreq.setCurrentAndTargetValue( logTarget );
        }
        else
        {
            glideLogFreq.reset( sampleRate, glideSec );
            glideLogFreq.setTargetValue( logTarget );
        }
        hasGlideTarget = true;

        releasing    = false;
        quickRelease = false;

        ampEnv.gate( 1 );
        filtEnv.gate( 1 );
        pitchEnv.gate( 1 );
        lfo.reset();

        // Base bookkeeping: state + velocity gain ramp. Its renderer
        // start() is null-slice safe; this voice never reads slices.
        // Oscillator phases are NOT reset — SEM VCOs free-run.
        ISynthVoice::noteOn( Number, Velocity );
    }

    void SynthVoice::noteOff()
    {
        ISynthVoice::noteOff();
        ampEnv.gate( 0 );
        filtEnv.gate( 0 );
        pitchEnv.gate( 0 );
        releasing = true;
    }

    void SynthVoice::forceQuickRelease()
    {
        // Design §5.3 steal path: 5 ms amp release (applied by
        // updateEnvelopes while quickRelease is set), gates off now;
        // process() frees the voice when the amp envelope reaches IDLE.
        quickRelease = true;
        ampEnv.gate( 0 );
        filtEnv.gate( 0 );
        pitchEnv.gate( 0 );
        releasing = true;
        if( isRunning() )
            state.store( State::STOPPING );
    }

    void SynthVoice::process( juce::AudioBuffer<float>& Buffer )
    {
        if( !isRunning() )
            return;

        updateEnvelopes();
        updateLFO();
        if( params.filterType() != cachedFilterType )
        {
            cachedFilterType = params.filterType();
            filter.setType( filterTypeFromInt( cachedFilterType ) );
        }
        filter.setResonance( juce::jlimit( 0.1f, 12.0f, (float)params.resonanceQ() ) );

        const auto masterGainL = master.gainL.load();
        const auto masterGainR = master.gainR.load();
        const auto numSamples  = Buffer.getNumSamples();

        auto out0 = tempBuffer.getWritePointer( 0 );
        auto out1 = tempBuffer.getWritePointer( 1 );

        // Deterministic per-voice spread (design §5.1): pool position
        // maps to -1..+1; detune +-spread cents, pan up to +-0.3.
        const double spreadFrac  = ( voiceIndex - 7.5 ) / 7.5;
        const double spreadCents = (double)params.spread() * spreadFrac;
        const float  spreadPan   = (float)( 0.3 * ( (double)params.spread() / 50.0 ) * spreadFrac );

        const float pitchDepthSemis = (float)params.pitchDepth() * ( 24.0f / 127.0f );
        const float envDepthOctaves = (float)params.filterDepth() / 16.0f;
        const double baseCutoff     = (double)params.cutoffHz();
        const double maxCutoff      = sampleRate * 0.49;
        const bool   lfoEnabled     = pLfo.isEnabled();

        for( auto i = 0; i < numSamples; ++i )
        {
            const float  lfoVal    = lfoEnabled ? lfo() : 0.0f; // bipolar -1..1
            const double ampE      = ampEnv();
            const double filtE     = filtEnv();
            const double pitchE    = pitchEnv();
            const double rGain     = gain.getNextValue(); // velocity ramp (base)

            const double semis = pitchE * pitchDepthSemis + (double)lfoVal * lfoToPitch + spreadCents / 100.0;
            const double freq  = std::exp2( glideLogFreq.getNextValue() ) * std::exp2( semis / 12.0 );

            double mix = 0.0;
            for( int k = 0; k < 3; ++k )
            {
                if( !params.osc[k].isEnabled() )
                    continue;
                const double ratio = std::exp2( ( params.osc[k].semi() + params.osc[k].fine() / 100.0 ) / 12.0 );
                osc[k].setFrequency( freq * ratio );
                osc[k].setWave( (PolyBlepOscillator::Wave)juce::jlimit( 0, 3, (int)params.osc[k].wave() ) );
                osc[k].setPulseWidth( 0.05 + ( params.osc[k].width() * MIDI::SCALER ) * 0.9 );
                mix += osc[k].process() * ( params.osc[k].level() * MIDI::SCALER );
            }
            mix *= kMixScale;

            // Filter envelope: cutoff x 2^(depth-scaled env + LFO).
            double cutoff = baseCutoff * std::exp2( filtE * envDepthOctaves + (double)lfoVal * lfoToFilter );
            cutoff        = juce::jlimit( 10.0, maxCutoff, cutoff );
            filter.setCutoffFrequency( (float)cutoff );
            const float filtered = filter.processSample( 0, (float)mix );

            // Tremolo: unipolarised depth law, gain in [1-depth, 1].
            const double amp = ampE * rGain * ( 1.0 - lfoToAmp * ( 0.5 - 0.5 * (double)lfoVal ) );
            const float  pan = juce::jlimit( -1.0f, 1.0f, spreadPan + lfoToPan * lfoVal );

            panner.setParameters( pan, amp );
            *out0++ = panner.getLeft() * filtered;
            *out1++ = panner.getRight() * filtered;
        }

        Buffer.addFrom( 0, 0, tempBuffer, 0, 0, numSamples, masterGainL );
        Buffer.addFrom( 1, 0, tempBuffer, 1, 0, numSamples, masterGainR );

        if( releasing && ampEnv.getCurrentState() == AudIO::ADSR<double>::IDLE )
            makeFree();
    }

} // namespace mt
