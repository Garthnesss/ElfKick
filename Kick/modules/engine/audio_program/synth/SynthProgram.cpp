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

#include "synth_voice.h"
// MTSynthProgram's complete type arrives via audio_program.h
// (source/mt_audio_synth_program.h), mirroring DrumProgram.cpp.

namespace mt
{
    SynthProgram::SynthProgram( AudIO::Design& D, const juce::String& Name, MTIProgram* PSynth )
        : ISynthObject( D, POLYPHONY::MAX )
    {
        setName( Name.toStdString() );

        // Pre-create the voice pool (mirrors DrumProgram).
        std::vector<std::unique_ptr<ISynthVoice>> voices;
        for( auto i = 0; i < getPolyphony(); ++i )
        {
            auto voice = std::make_unique<SynthVoice>( master, i );
            voicePtrs.push_back( voice.get() );
            voices.push_back( std::move( voice ) );
        }
        ISynthObject::addVoices( voices );

        setProgram( PSynth );
    }

    SynthProgram::~SynthProgram()
    {
        setBypass( true );
    }

    void SynthProgram::setProgram( MTIProgram* PSynth )
    {
        ISynthObject::setProgram( PSynth );

        noteOffAll();
        pending.clear();
        pSynth = dynamic_cast<MTSynthProgram*>( PSynth );
        setBypass( ( pSynth == nullptr ) );

        if( pSynth )
        {
            auto synthTree = pSynth->getState().getChildWithName( IDs::SYNTH );
            for( auto* voice : voicePtrs )
                voice->attachSynth( synthTree );
        }
    }

    void SynthProgram::initialize( uint32_t SampleRate, uint32_t BlockSize )
    {
        for( auto* voice : voicePtrs )
            voice->prepareToPlay( (double)SampleRate, (int)BlockSize );
    }

    void SynthProgram::startVoice( SynthVoice* Voice, int Note, int Velocity )
    {
        Voice->startStamp = ++stampCounter;
        Voice->noteOn( Note, Velocity );
        addVoiceForPlayback( Voice );
    }

    void SynthProgram::noteOn( int Note, int Velocity )
    {
        if( !pSynth || !CommonUtils::isValidMidiNote( Note ) )
            return;

        // (1) A voice already playing this note retriggers.
        if( auto* playing = isPlayback( Note ) )
        {
            startVoice( static_cast<SynthVoice*>( playing ), Note, Velocity );
            return;
        }

        // (2) A free voice.
        if( auto* freeVoice = getFreeVoice( Note, 1 ) )
        {
            startVoice( static_cast<SynthVoice*>( freeVoice ), Note, Velocity );
            return;
        }

        // (3) Steal: prefer a voice already releasing (smallest stamp =
        // longest releasing), else the oldest running voice. The victim
        // gets a 5 ms quick release; the note is parked and started by
        // doProcess() as soon as a voice is free (<= one block later
        // than the victim's envelope reaching IDLE).
        SynthVoice* victim = nullptr;
        for( auto* voice : voicePtrs )
            if( voice->isReleasing() && ( !victim || voice->startStamp < victim->startStamp ) )
                victim = voice;
        if( !victim )
            for( auto* voice : voicePtrs )
                if( voice->isRunning() && ( !victim || voice->startStamp < victim->startStamp ) )
                    victim = voice;

        if( victim )
        {
            victim->forceQuickRelease();
            if( pending.size() < (size_t)getPolyphony() )
                pending.emplace_back( Note, Velocity );
        }
    }

    void SynthProgram::doProcess( juce::AudioBuffer<float>& )
    {
        // Start any notes parked by the steal path once a voice is free.
        while( !pending.empty() )
        {
            auto [note, velocity] = pending.front();
            if( auto* playing = isPlayback( note ) )
            {
                startVoice( static_cast<SynthVoice*>( playing ), note, velocity );
            }
            else if( auto* freeVoice = getFreeVoice( note, 1 ) )
            {
                startVoice( static_cast<SynthVoice*>( freeVoice ), note, velocity );
            }
            else
            {
                break; // still no free voice; retry next block
            }
            pending.erase( pending.begin() );
        }
    }

} // namespace mt
