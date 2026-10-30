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

// ElfKick addition (not upstream Kick code) — SYNTH_DESIGN.md §5.4/§6.
//
// ValueTree atomics for the synth program's SYNTH subtree. The synth owns
// its envelope/filter atomics rather than reusing MTIAtomicEnvelopes:
// the stock MTIAtomicFilterEnvelope attaches TWO atomics to the same
// `type` property (adsrType via MTIAtomicADSR and its own filter `type`)
// and types cutoff/resonance as ints (cutoff default 20000 there, but
// DrumProgram's factory XML writes 8500.0 — the units are ambiguous
// upstream). The synth schema (design §6.2) is explicit: ADSR times are
// float SECONDS, cutoff is float Hz, resonance is float Q, and `type` on
// FILTERENV is the filter type. Property names match the stock ones so
// the XML reads like the other programs'.

#pragma once

namespace mt
{

    struct MTIAtomicSynthOsc
    {
        // Defaults per design §6.2 are supplied by MTIAtomicSynthParams
        // (they differ per oscillator index).
        void attachTo( juce::ValueTree Tree, bool DefEnabled, int DefWave, int DefSemi, int DefFine, int DefLevel, int DefWidth )
        {
            auto Undo = nullptr;
            isEnabled.attachTo( Tree, IDs::isEnabled, Undo, DefEnabled );
            wave.attachTo( Tree, IDs::wave, Undo, DefWave );
            semi.attachTo( Tree, IDs::semi, Undo, DefSemi );
            fine.attachTo( Tree, IDs::fine, Undo, DefFine );
            level.attachTo( Tree, IDs::level, Undo, DefLevel );
            width.attachTo( Tree, IDs::width, Undo, DefWidth );
        }

        Atomic<bool> isEnabled;
        Atomic<int>  wave;  // PolyBlepOscillator::Wave: 0 saw, 1 pulse, 2 tri, 3 sine
        Atomic<int>  semi;  // semitones, -24..24
        Atomic<int>  fine;  // cents, -100..100
        Atomic<int>  level; // 0..127
        Atomic<int>  width; // 0..127 -> pulse width 0.05..0.95
    };

    struct MTIAtomicSynthADSR
    {
        void attachTo( juce::ValueTree Tree, float DefA, float DefH, float DefD, float DefS, float DefR )
        {
            auto Undo = nullptr;
            a.attachTo( Tree, IDs::attack, Undo, DefA );
            h.attachTo( Tree, IDs::hold, Undo, DefH );
            d.attachTo( Tree, IDs::decay, Undo, DefD );
            s.attachTo( Tree, IDs::sustain, Undo, DefS );
            r.attachTo( Tree, IDs::release, Undo, DefR );
        }

        // Stage times in float SECONDS (AudIO::ADSR native units).
        Atomic<float> a;
        Atomic<float> h;
        Atomic<float> d;
        Atomic<float> s; // 0..1
        Atomic<float> r;
    };

    struct MTIAtomicSynthParams
    {
        explicit MTIAtomicSynthParams( juce::ValueTree Tree = juce::ValueTree{ IDs::SYNTH } ) { attachTo( Tree ); }

        void attachTo( juce::ValueTree Tree )
        {
            auto Undo = nullptr;
            glide.attachTo( Tree, IDs::glide, Undo, 0.0f ); // seconds, 0 = off
            spread.attachTo( Tree, IDs::spread, Undo, 0 );  // cents, 0..50, 0 = off

            // Oscillators: OSC children selected by their `index` property;
            // a missing child attaches to a detached tree so the per-index
            // defaults below apply (design §6.2 fallback rule).
            //            enabled wave semi fine level width
            osc[0].attachTo( findOsc( Tree, 0 ), true, 0, 0, 0, 110, 64 );
            osc[1].attachTo( findOsc( Tree, 1 ), true, 0, 0, 7, 90, 64 );
            osc[2].attachTo( findOsc( Tree, 2 ), true, 1, -12, 0, 70, 64 );

            auto envTree = Tree.getChildWithName( IDs::ENVELOPE );
            ampEnv.attachTo( envTree.getChildWithName( IDs::AMPENV ), 0.005f, 0.0f, 0.25f, 0.8f, 0.15f );
            filterEnv.attachTo( envTree.getChildWithName( IDs::FILTERENV ), 0.005f, 0.0f, 0.3f, 0.4f, 0.2f );
            pitchEnv.attachTo( envTree.getChildWithName( IDs::PITCHENV ), 0.005f, 0.0f, 0.2f, 0.0f, 0.1f );

            auto filtTree = envTree.getChildWithName( IDs::FILTERENV );
            filterType.attachTo( filtTree, IDs::type, Undo, 0 );          // 0 LP, 1 HP, 2 BP
            cutoffHz.attachTo( filtTree, IDs::freq, Undo, 8500.0f );      // Hz
            resonanceQ.attachTo( filtTree, IDs::resonance, Undo, 0.7f );  // Q
            filterDepth.attachTo( filtTree, IDs::depth, Undo, 48 );       // /16 = octaves (48 = +3)

            auto pitchTree = envTree.getChildWithName( IDs::PITCHENV );
            pitchDepth.attachTo( pitchTree, IDs::depth, Undo, 0 ); // -127..127
        }

        static juce::ValueTree findOsc( juce::ValueTree SynthTree, int Index )
        {
            for( auto child : SynthTree )
                if( child.hasType( IDs::OSC ) && (int)child.getProperty( IDs::index, -1 ) == Index )
                    return child;
            return juce::ValueTree{ IDs::OSC };
        }

        Atomic<float> glide;
        Atomic<int>   spread;

        MTIAtomicSynthOsc  osc[3];
        MTIAtomicSynthADSR ampEnv;
        MTIAtomicSynthADSR filterEnv;
        MTIAtomicSynthADSR pitchEnv;

        Atomic<int>   filterType;
        Atomic<float> cutoffHz;
        Atomic<float> resonanceQ;
        Atomic<int>   filterDepth;
        Atomic<int>   pitchDepth;
    };

} // namespace mt
