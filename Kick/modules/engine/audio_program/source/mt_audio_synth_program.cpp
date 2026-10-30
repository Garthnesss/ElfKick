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
// Mirrors MTDrumProgram's constructor/destructor wiring exactly, minus
// rebuildSampleMap() (a synth has no samples to map).

#include "mt_audio_synth_program.h"

namespace mt
{
    MTSynthProgram::MTSynthProgram( AudIO::DawEngine& Engine, juce::ValueTree& VT, juce::UndoManager* UM )
        : MTCommonProgram( VT, UM ), engine( Engine )
    {
        auto pProg = std::make_unique<SynthProgram>( Engine, getName().toStdString(), this );
        if( pProg )
        {
            synth = pProg.get();
            Engine.createObject( std::move( pProg ) );
            synth->initAudioRouting();

            auto outBusId = engine.getBusManager()->getBusId( getOutput().toStdString() );
            synth->connectOutputTo( outBusId );
            synth->setGainsForSend( getSendGains() );

            onNameChanged = [&]() {
                synth->setName( getName().toStdString() );
            };

            onOutChanged = [&]() {
                auto outBusId = engine.getBusManager()->getBusId( getOutput().toStdString() );
                synth->connectOutputTo( outBusId );
            };

            onSendGainChanged = [&]( int Channel, float Value ) {
                synth->setGainForSend( (uint32_t)Channel, Value );
            };

            // Inserts...
            auto vt = VT.getOrCreateChildWithName( IDs::INSERTS, nullptr );
            MTIInsertsInterface::setup( vt, UM, synth );
        }
    }

    MTSynthProgram::~MTSynthProgram()
    {
        if( synth )
        {
            engine.removeObject( synth->getId() );
        }
    }

} // namespace mt
