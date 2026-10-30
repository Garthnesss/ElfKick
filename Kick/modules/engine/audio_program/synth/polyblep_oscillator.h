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

// ElfKick addition (not upstream Kick code).
//
// PolyBlepOscillator — the one DSP component written for the ElfKick synth
// (see SYNTH_DESIGN.md §3/§5.2). A small, self-contained, JUCE-free
// band-limited oscillator: saw / pulse (variable width) / triangle / sine,
// double-precision phase accumulator, PolyBLEP residuals on the
// discontinuities. Modelled on Garth's house implementations:
// elfsynth-core `osc.rs` (poly_blep / saw_blep / square_blep) and the
// ElfPro800 plugin's `polyblep`/`waveSaw`/`wavePulse`/`waveTri`
// (PluginProcessor.cpp) — the formulations agree exactly.
//
// AudIO::LFO was judged and rejected for the VCO role (nearest-neighbour
// wavetables as compiled; square fixed at 50% duty — no PWM, and PWM is
// signature SEM). No allocations, no tables, no platform code; safe to
// call from the audio thread. Frequency may be updated per sample
// (pitch envelopes / vibrato / glide are the voice's job).

#pragma once

#include <cmath>

namespace mt
{

    class PolyBlepOscillator
    {
    public:
        // Waveform ids match the SYNTH schema enum (SYNTH_DESIGN.md §6.2).
        enum class Wave
        {
            Saw      = 0,
            Pulse    = 1,
            Triangle = 2,
            Sine     = 3
        };

        void prepare( double sampleRate ) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            updateIncrement();
        }

        void setFrequency( double hz ) noexcept
        {
            freqHz = hz;
            updateIncrement();
        }

        double getFrequency() const noexcept { return freqHz; }
        double getSampleRate() const noexcept { return sr; }

        void   setWave( Wave w ) noexcept { wave = w; }
        Wave   getWave() const noexcept { return wave; }

        // Pulse width, clamped to the schema's 0.05..0.95 range.
        void  setPulseWidth( double w ) noexcept { pulseWidth = w < 0.05 ? 0.05 : ( w > 0.95 ? 0.95 : w ); }
        double getPulseWidth() const noexcept { return pulseWidth; }

        void   resetPhase( double p = 0.0 ) noexcept { phase = p - std::floor( p ); }
        double getPhase() const noexcept { return phase; }

        // Render one sample at the current phase, then advance.
        float process() noexcept
        {
            const double ph = phase;
            const double dt = increment;
            double       y  = 0.0;

            switch( wave )
            {
                case Wave::Saw:
                    y = 2.0 * ph - 1.0 - polyblep( ph, dt );
                    break;

                case Wave::Pulse:
                {
                    y = ph < pulseWidth ? 1.0 : -1.0;
                    y += polyblep( ph, dt ); // rising edge at phase 0
                    double t2 = ph - pulseWidth;
                    if( t2 < 0.0 ) t2 += 1.0;
                    y -= polyblep( t2, dt ); // falling edge at the width
                    break;
                }

                case Wave::Triangle:
                    // House formulation (ElfPro800 waveTri): naive triangle;
                    // it has no discontinuity, so no BLEP is needed.
                    y = ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
                    break;

                case Wave::Sine:
                    y = std::sin( twoPi * ph );
                    break;
            }

            phase += dt;
            if( phase >= 1.0 ) phase -= std::floor( phase );
            return (float)y;
        }

    private:
        // PolyBLEP residual for a unit discontinuity at phase 0, copied in
        // formulation from elfsynth-core osc.rs / ElfPro800 (the standard
        // t/(1-t) polynomial BLEP). Returns the correction to *subtract*
        // at a falling edge / *add* at a rising edge.
        static double polyblep( double t, double dt ) noexcept
        {
            if( dt <= 0.0 ) return 0.0;
            if( t < dt )
            {
                const double x = t / dt;
                return x + x - x * x - 1.0;
            }
            if( t > 1.0 - dt )
            {
                const double x = ( t - 1.0 ) / dt;
                return x * x + x + x + 1.0;
            }
            return 0.0;
        }

        void updateIncrement() noexcept
        {
            // Clamp below Nyquist so the BLEP windows never overlap.
            const double maxFreq = sr * 0.49;
            const double f       = freqHz < 0.0 ? 0.0 : ( freqHz > maxFreq ? maxFreq : freqHz );
            increment            = f / sr;
        }

        static constexpr double twoPi = 6.28318530717958647692;

        double sr         = 48000.0;
        double freqHz     = 440.0;
        double increment  = 440.0 / 48000.0;
        double phase      = 0.0;
        double pulseWidth = 0.5;
        Wave   wave       = Wave::Saw;
    };

} // namespace mt
