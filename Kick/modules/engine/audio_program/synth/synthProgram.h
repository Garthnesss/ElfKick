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

// ElfKick addition (not upstream Kick code) — SYNTH_DESIGN.md §4.2/§5.3.
//
// SynthProgram: the ISynthObject half of the synth program (DrumProgram's
// counterpart). Owns the 16-voice SynthVoice pool and the allocation
// policy: retrigger a voice already playing the note, else take a free
// voice, else steal (quick-release the victim, park the note, allocate
// it in doProcess once the victim's envelope has finished — stock
// ISynthObject::getFreeVoice's steal branch is an empty comment).

#pragma once

#include "iSynthObject.h"

namespace mt
{
    class MTSynthProgram;
    class SynthVoice;

    class SynthProgram : public ISynthObject
    {
    public:
        explicit SynthProgram( AudIO::Design& D, const juce::String& Name, MTIProgram* PSynth );
        ~SynthProgram();

        void setProgram( MTIProgram* PSynth ) override;
        void noteOn( int Note, int Velocity ) override;
        void initialize( uint32_t SampleRate, uint32_t BlockSize ) override;
        void doProcess( juce::AudioBuffer<float>& Buffer ) override;

    private:
        void startVoice( SynthVoice* Voice, int Note, int Velocity );

        MTSynthProgram*                        pSynth{ nullptr };
        std::vector<SynthVoice*>               voicePtrs; // non-owning views (owned via ISynthObject::addVoices)
        std::vector<std::pair<int, int>>       pending;   // notes waiting for a stolen voice to free
        uint64_t                               stampCounter{ 0 };
    };
}; // namespace mt
