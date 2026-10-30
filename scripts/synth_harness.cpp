/*
 * ElfKick — synth oscillator render harness (SYNTH_DESIGN.md Step 1/2 tool).
 *
 * Standalone console program: pumps mt::PolyBlepOscillator::process()
 * manually (no audio device, no JUCE) and writes 16-bit PCM WAV renders
 * for offline measurement by scripts/analyze_synth_render.py.
 *
 * Usage: synth_harness [output-dir]     (default: synth-renders)
 *
 * Renders (48 kHz, gate = 1.0 s on, 0.3 s off, unless noted):
 *   saw_a440.wav  pulse50_a440.wav  pulse25_a440.wav
 *   tri_a440.wav  sine_a440.wav     gated_off.wav (gate never opens)
 *
 * AGPL-3.0-or-later, (c) 2026 ElfKick contributors.
 */

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "polyblep_oscillator.h"

namespace
{
    constexpr double kSampleRate = 48000.0;

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
    }

    // Render one gated oscillator run: gateOn seconds of osc * 1.0,
    // then gateOff seconds of exact silence (the component-level gate;
    // envelope shaping is the voice's job and is proven at the voice step).
    std::vector<float> render( mt::PolyBlepOscillator::Wave wave, double freq, double width,
                               double gateOnSec, double gateOffSec )
    {
        mt::PolyBlepOscillator osc;
        osc.prepare( kSampleRate );
        osc.setWave( wave );
        osc.setFrequency( freq );
        osc.setPulseWidth( width );

        const int onN  = (int)std::llround( gateOnSec * kSampleRate );
        const int offN = (int)std::llround( gateOffSec * kSampleRate );
        std::vector<float> out;
        out.reserve( (size_t)( onN + offN ) );
        for( int i = 0; i < onN; ++i ) out.push_back( osc.process() );
        for( int i = 0; i < offN; ++i ) { (void)osc.process(); out.push_back( 0.0f ); }
        return out;
    }
} // namespace

int main( int argc, char** argv )
{
    const std::string dir = argc > 1 ? argv[1] : "synth-renders";
    const std::string p   = dir + "/";

    writeWav( p + "saw_a440.wav", render( mt::PolyBlepOscillator::Wave::Saw, 440.0, 0.5, 1.0, 0.3 ) );
    writeWav( p + "pulse50_a440.wav", render( mt::PolyBlepOscillator::Wave::Pulse, 440.0, 0.50, 1.0, 0.3 ) );
    writeWav( p + "pulse25_a440.wav", render( mt::PolyBlepOscillator::Wave::Pulse, 440.0, 0.25, 1.0, 0.3 ) );
    writeWav( p + "tri_a440.wav", render( mt::PolyBlepOscillator::Wave::Triangle, 440.0, 0.5, 1.0, 0.3 ) );
    writeWav( p + "sine_a440.wav", render( mt::PolyBlepOscillator::Wave::Sine, 440.0, 0.5, 1.0, 0.3 ) );

    // Gate never opens: the harness writes osc.process() * 0 — expect digital silence.
    {
        mt::PolyBlepOscillator osc;
        osc.prepare( kSampleRate );
        osc.setFrequency( 440.0 );
        std::vector<float> out( 24000 );
        for( auto& s : out ) { (void)osc.process(); s = 0.0f; }
        writeWav( p + "gated_off.wav", out );
    }

    // Soft CPU check (design §10): wall time to render 16 s of saw.
    {
        mt::PolyBlepOscillator osc;
        osc.prepare( kSampleRate );
        osc.setWave( mt::PolyBlepOscillator::Wave::Saw );
        osc.setFrequency( 440.0 );
        const int  n     = 48000 * 16;
        double     sink  = 0.0;
        const auto t0    = std::chrono::steady_clock::now();
        for( int i = 0; i < n; ++i ) sink += osc.process();
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
        std::printf( "cpu: rendered 16.0 s of saw in %lld ms (%.1f%% of one core at realtime) sink=%.3f\n",
                     (long long)ms, 100.0 * (double)ms / 16000.0, sink );
    }

    std::printf( "wrote renders to %s\n", dir.c_str() );
    return 0;
}
