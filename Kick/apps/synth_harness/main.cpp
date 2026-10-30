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

// ElfKick addition — SYNTH_DESIGN.md Step 2 offline render proof.
//
// Deterministic harness: constructs mt::SynthVoice objects directly
// against a hand-built PROGRAM ValueTree carrying the §6 schema (no
// audio device, no DawEngine, no message loop), pumps process() block
// by block at 48000 Hz / 960 samples, and writes WAV renders for
// scripts/analyze_synth_voice.py to measure.
//
// Usage: SynthHarness [output-dir]   (default: synth-voice-renders)
// Renders: voice_saw_a440.wav (osc 1 only), voice_full_a440.wav,
//          voice_triad.wav (notes 60/64/67), voice_cutoff_low.wav
//          (filter freq 200 Hz), voice_cutoff_high.wav (12000 Hz).

#include <audio_program/audio_program.h>
#include <audio_program/synth/synth_voice.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 960;

    using namespace mt;

    juce::ValueTree makeOsc( int index, bool enabled, int wave, int semi, int fine, int level, int width )
    {
        juce::ValueTree osc{ IDs::OSC };
        osc.setProperty( IDs::index, index, nullptr );
        osc.setProperty( IDs::isEnabled, enabled, nullptr );
        osc.setProperty( IDs::wave, wave, nullptr );
        osc.setProperty( IDs::semi, semi, nullptr );
        osc.setProperty( IDs::fine, fine, nullptr );
        osc.setProperty( IDs::level, level, nullptr );
        osc.setProperty( IDs::width, width, nullptr );
        return osc;
    }

    juce::ValueTree makeEnv( juce::Identifier Type, double a, double h, double d, double s, double r )
    {
        juce::ValueTree env{ Type };
        env.setProperty( IDs::attack, a, nullptr );
        env.setProperty( IDs::hold, h, nullptr );
        env.setProperty( IDs::decay, d, nullptr );
        env.setProperty( IDs::sustain, s, nullptr );
        env.setProperty( IDs::release, r, nullptr );
        return env;
    }

    // The §6 schema, built programmatically. osc1Only isolates oscillator
    // 1 for the strict frequency/harmonic measurements; cutoffHz varies
    // the FILTERENV freq for the A/B centroid proof.
    juce::ValueTree makeProgramTree( bool osc1Only, double cutoffHz )
    {
        juce::ValueTree program{ IDs::PROGRAM };
        program.setProperty( IDs::name, "Harness Synth", nullptr );
        program.setProperty( IDs::type, 8, nullptr );

        juce::ValueTree synth{ IDs::SYNTH };
        synth.setProperty( IDs::glide, 0.0f, nullptr );
        synth.setProperty( IDs::spread, 0, nullptr );
        synth.appendChild( makeOsc( 0, true, 0, 0, 0, 110, 0 ), nullptr );
        synth.appendChild( makeOsc( 1, !osc1Only, 0, 0, 7, 90, 0 ), nullptr );
        synth.appendChild( makeOsc( 2, !osc1Only, 1, -12, 0, 70, 64 ), nullptr );

        juce::ValueTree env{ IDs::ENVELOPE };
        env.appendChild( makeEnv( IDs::AMPENV, 0.005, 0.0, 0.25, 0.8, 0.30 ), nullptr );
        auto filt = makeEnv( IDs::FILTERENV, 0.010, 0.0, 0.40, 0.4, 0.30 );
        filt.setProperty( IDs::type, 0, nullptr );
        filt.setProperty( IDs::freq, cutoffHz, nullptr );
        filt.setProperty( IDs::resonance, 0.7, nullptr );
        filt.setProperty( IDs::depth, 48, nullptr );
        env.appendChild( filt, nullptr );
        auto pitch = makeEnv( IDs::PITCHENV, 0.005, 0.0, 0.20, 0.0, 0.20 );
        pitch.setProperty( IDs::depth, 0, nullptr );
        env.appendChild( pitch, nullptr );
        synth.appendChild( env, nullptr );

        juce::ValueTree lfo{ IDs::LFO };
        lfo.setProperty( IDs::isEnabled, false, nullptr );
        lfo.setProperty( IDs::type, 0, nullptr );
        lfo.setProperty( IDs::freq, 5.0f, nullptr );
        synth.appendChild( lfo, nullptr );

        program.appendChild( synth, nullptr );
        return program;
    }

    // Render a set of simultaneous notes through fresh SynthVoices.
    std::vector<float> renderNotes( const std::vector<int>& notes, bool osc1Only, double cutoffHz,
                                    int gateBlocks, int tailBlocks )
    {
        auto program = makeProgramTree( osc1Only, cutoffHz );

        MTIAtomicProgramMaster master;
        master.attachTo( program );
        // Fire the pitch-ratio callback explicitly (mirrors a program
        // tree carrying authored master values).
        program.setProperty( IDs::semi, 0, nullptr );
        program.setProperty( IDs::fine, 0, nullptr );

        std::vector<std::unique_ptr<SynthVoice>> voices;
        for( size_t i = 0; i < notes.size(); ++i )
        {
            auto voice = std::make_unique<SynthVoice>( master, (int)i );
            voice->attachSynth( program.getChildWithName( IDs::SYNTH ) );
            voice->prepareToPlay( kSampleRate, kBlock );
            voices.push_back( std::move( voice ) );
        }

        std::vector<float> out;
        out.reserve( (size_t)( gateBlocks + tailBlocks ) * kBlock );
        juce::AudioBuffer<float> mix( 2, kBlock );

        for( int block = 0; block < gateBlocks + tailBlocks; ++block )
        {
            if( block == 0 )
                for( size_t i = 0; i < notes.size(); ++i )
                    voices[i]->noteOn( notes[i], 100 );
            if( block == gateBlocks )
                for( auto& voice : voices )
                    voice->noteOff();

            mix.clear();
            for( auto& voice : voices )
                voice->process( mix );

            auto* ch0 = mix.getReadPointer( 0 );
            out.insert( out.end(), ch0, ch0 + kBlock );
        }
        return out;
    }

    void writeWav( const std::string& path, const std::vector<float>& samples )
    {
        FILE* f = std::fopen( path.c_str(), "wb" );
        if( !f ) { std::fprintf( stderr, "cannot write %s\n", path.c_str() ); std::exit( 2 ); }
        const uint32_t dataBytes = (uint32_t)samples.size() * 2;
        auto w32 = [&]( uint32_t v ) { std::fwrite( &v, 4, 1, f ); };
        auto w16 = [&]( uint16_t v ) { std::fwrite( &v, 2, 1, f ); };
        std::fwrite( "RIFF", 1, 4, f ); w32( 36 + dataBytes );
        std::fwrite( "WAVEfmt ", 1, 8, f ); w32( 16 ); w16( 1 ); w16( 1 );
        w32( (uint32_t)kSampleRate ); w32( (uint32_t)kSampleRate * 2 ); w16( 2 ); w16( 16 );
        std::fwrite( "data", 1, 4, f ); w32( dataBytes );
        for( float s : samples )
        {
            double c = s < -1.0 ? -1.0 : ( s > 1.0 ? 1.0 : s );
            auto   v = (int16_t)std::lround( c * 32767.0 );
            std::fwrite( &v, 2, 1, f );
        }
        std::fclose( f );

        double peak = 0.0, sumSq = 0.0;
        for( float s : samples ) { peak = std::max( peak, (double)std::fabs( s ) ); sumSq += (double)s * s; }
        const double rms = samples.empty() ? 0.0 : std::sqrt( sumSq / (double)samples.size() );
        std::printf( "  %s: %zu samples, peak %.3f (%.2f dBFS), rms %.2f dBFS\n", path.c_str(),
                     samples.size(), peak, 20.0 * std::log10( peak + 1e-12 ), 20.0 * std::log10( rms + 1e-12 ) );
    }
} // namespace

int main( int argc, char** argv )
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const std::string dir = argc > 1 ? argv[1] : "synth-voice-renders";
    const std::string p   = dir + "/";

    std::printf( "synth voice harness renders -> %s\n", dir.c_str() );
    const auto t0 = std::chrono::steady_clock::now();

    // 60 blocks gate (1.2 s) + 40 blocks tail (0.8 s) unless noted.
    writeWav( p + "voice_saw_a440.wav", renderNotes( { 69 }, true, 8500.0, 60, 40 ) );
    writeWav( p + "voice_full_a440.wav", renderNotes( { 69 }, false, 8500.0, 60, 40 ) );
    writeWav( p + "voice_triad.wav", renderNotes( { 60, 64, 67 }, false, 8500.0, 60, 40 ) );
    writeWav( p + "voice_cutoff_low.wav", renderNotes( { 69 }, true, 200.0, 50, 15 ) );
    writeWav( p + "voice_cutoff_high.wav", renderNotes( { 69 }, true, 12000.0, 50, 15 ) );

    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
    std::printf( "cpu: 9.3 voice-seconds rendered in %lld ms (%.2f%% of one core at realtime)\n",
                 (long long)ms, 100.0 * (double)ms / 9300.0 );
    return 0;
}
