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

// ElfKick addition (not upstream Kick code) — SYNTH_DESIGN.md §4.1.
//
// MTSynthProgram: MTIProgram half of the synth program (MTDrumProgram's
// counterpart). Creates the SynthProgram engine object, registers it
// with the DawEngine, wires output/sends/inserts, forwards notes and
// meters. Deliberately NOT overridden: iSampleCreator (a synth has no
// samples; the base returns nullptr — see the Step 0 audit, hazard H2,
// for the null-tolerance analysis) and the sample-map machinery.

#pragma once

#include "mt_audio_common_program.h"

namespace mt
{
    class SynthProgram;

    class MTSynthProgram : public MTCommonProgram
    {
    public:
        explicit MTSynthProgram( AudIO::DawEngine& Engine, juce::ValueTree& VT, juce::UndoManager* UM = nullptr );
        ~MTSynthProgram();

        // A synth sustains while the gate is held: NOTE_ON, not the
        // ONE_SHOT that MTCommonProgram reports (design §2.2 — note
        // dispatch keys off this virtual).
        SamplePlayType getNoteSamplePlay( int ) override { return SamplePlayType::NOTE_ON; }

        // Metering....
        void               enableOutputMetering( bool Enable ) override { synth->enableOutputMetering( Enable ); }
        const MTMeterData& readOutMeter() const override { return synth->readOutMeter(); };

        // IMidiMsgProcessor
        void noteOn( int Note, int Velocity ) override { synth->noteOn( Note, Velocity ); };
        void noteOff( int Note ) override { synth->noteOff( Note ); };
        void noteOff( std::vector<int>& Notes ) override { synth->noteOff( Notes ); };
        void noteOffAll() override { synth->noteOffAll(); };
        void noteMute( int Note, bool Mute ) override { synth->noteMute( Note, Mute ); };
        void noteReset( int Note ) override { synth->noteReset( Note ); };
        void noteReset( std::vector<int>& Notes ) override { synth->noteReset( Notes ); };
        void noteLoop( int Note, bool Loop ) override { synth->noteLoop( Note, Loop ); };

    private:
        AudIO::DawEngine& engine;
        SynthProgram*     synth{ nullptr };
    };

} // namespace mt
