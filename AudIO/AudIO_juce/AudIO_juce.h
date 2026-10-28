#pragma once
/*
  BEGIN_JUCE_MODULE_DECLARATION
  ID:                 AudIO_juce
  vendor:             MUSIC Tribe, MY
  version:            0.1.0
  name:               MusicTribe AudIO JUCE extensions
  description:        JUCE extensions for AudIO (RECONSTRUCTED - not in public release; the public AudIO.h AUDIO_JUCE_EXTENSIONS block is empty)
  license:            AGPLv3
  dependencies:       AudIO, juce_audio_devices, juce_audio_processors
  END_JUCE_MODULE_DECLARATION
*/

/*
  RECONSTRUCTED (ElfKick, Oct 2026): the private AudIO_juce module was not
  part of the public release. The classes below are rebuilt from the call
  sites in the Kick tree (modules/engine/audio_engine) on top of the public
  AudIO::DRecorder / AudIO::AudioRecorder / AudIO::IAudioDevice:

  - mt::Recorder      — alias of AudIO::AudioRecorder (its Direction/Mode
                        enums are what the looper interface uses).
  - mt::SampleRecorder / mt::Looper — AudIO::DRecorder plus the name
                        adaptations and small extras the Kick code calls
                        (setEnable, setGainAndPan, startRecording, looper
                        transport helpers, a juce::AudioBuffer view).
  - mt::AudioDevice   — AudIO::IAudioDevice backed by a
                        juce::AudioDeviceManager; on a machine with no
                        sound card initialise() fails silently and the
                        engine simply never gets callbacks.

  Behavioural fidelity beyond compile/link/launch is NOT claimed: e.g.
  undo/redo on the looper are no-ops and getAudioBuffer() copies the
  recorder's data into a member buffer on each call.
*/

#include <AudIO/AudIO.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cmath>

namespace mt
{
    using Recorder = AudIO::AudioRecorder;

    //
    // Shared base for SampleRecorder / Looper: a DRecorder with the Kick-side
    // method names mapped onto AudioRecorder's API.
    //
    class RecorderBase : public AudIO::DRecorder
    {
    public:
        explicit RecorderBase( AudIO::Design& D )
            : AudIO::DRecorder( D )
        {
        }

        // Enable flag (AudIO::Object has none).
        void setEnable( bool Enable ) { _enabled.store( Enable ); }
        bool isEnabled() const { return _enabled.load(); }

        // Name adaptations onto AudioRecorder.
        Mode      getRecordingMode() { return getRecordMode(); }
        void      setRecordingMode( Mode M ) { setRecordMode( M ); }
        float     getGain() { return getVolume(); }
        void      setGain( float Gain ) { setVolume( Gain ); }
        void      setGainDB( float GainDB ) { setVolumeDB( GainDB ); }
        void      setMonitorOn( bool Monitor ) { setMonitor( Monitor ); }
        bool      isForward() { return isForwardDirection(); }
        void      setGainAndPan( float Gain, float Pan, bool /*Immediate*/ ) { setVolumeAndPan( Gain, Pan ); }

        void setLength( double timeInSeconds )
        {
            _lengthSeconds = timeInSeconds;
            AudIO::AudioRecorder::setLength( timeInSeconds );
        }
        float getLength() const { return (float)_lengthSeconds; }

        void startRecording( bool FromBeginning ) { start( getRecordMode(), getDirection(), FromBeginning ); }
        void stopRecording() { stop(); }
        void saveRecording( const std::string& FilePath ) { save( FilePath ); }

        // JUCE view of the recorded data (copied out on each call).
        juce::AudioBuffer<float>& getAudioBuffer()
        {
            const auto& data = getRecorderData();
            _juceBuffer.setSize( (int)data.size(), data.empty() ? 0 : (int)data[0].size(), false, true );
            for( size_t ch = 0; ch < data.size(); ++ch )
                if( !data[ch].empty() )
                    _juceBuffer.copyFrom( (int)ch, 0, data[ch].data(), (int)data[ch].size() );
            return _juceBuffer;
        }

    private:
        std::atomic<bool>        _enabled{ false };
        double                   _lengthSeconds{ 0 };
        juce::AudioBuffer<float> _juceBuffer;
    };

    //
    // RECONSTRUCTED: JUCE-flavoured DSP helpers the synth voices use
    // (modules/engine/audio_program/synth/drum_voice.h: ADSR, Filter,
    // FilterType). Built on the public AudIO DSP classes.
    //
    class ADSR : public AudIO::ADSR<double>
    {
    public:
        using AudIO::ADSR<double>::ADSR;

        // drum_voice passes (A, H, D, R, S) as separate values, in samples.
        void setParameters( double A, double H, double D, double R, double S )
        {
            AudIO::ADSR<double>::setParameters( typename AudIO::ADSR<double>::Params{ A, H, D, S, R } );
        }

        double operator()() { return process(); }
    };

    enum class FilterType
    {
        LowPass = 0,
        HighPass,
        BandPass
    };

    // NOTE: not built on AudIO::Filter — that class is bit-rotted upstream
    // (empty `override` stubs for nextSample/processNext/getNext that don't
    // override anything). This wraps juce::dsp::StateVariableTPTFilter.
    class Filter
    {
    public:
        // Cutoff range in Hz (log-ish); convertTo0to1() is what drum_voice uses.
        inline static const juce::NormalisableRange<double> freqRange{ 20.0, 20000.0, 0.0, 0.25 };

        void setSampleRate( double SampleRate )
        {
            _sampleRate = SampleRate;
            juce::dsp::ProcessSpec spec{ SampleRate, 512, 2 };
            _filter.prepare( spec );
            applyCutoff();
        }

        void reset() { _filter.reset(); }

        void setFilterType( FilterType Type )
        {
            _filter.setType( Type == FilterType::HighPass ? juce::dsp::StateVariableTPTFilterType::highpass
                             : Type == FilterType::BandPass ? juce::dsp::StateVariableTPTFilterType::bandpass
                                                             : juce::dsp::StateVariableTPTFilterType::lowpass );
        }

        void setResonance( double Resonance ) { _filter.setResonance( (float)juce::jlimit( 0.1, 10.0, Resonance <= 0.0 ? 0.707 : Resonance ) ); }

        // drum_voice passes a normalised 0..1 cutoff (freqRange space).
        void setCutoff( double Normalised )
        {
            _normalisedCutoff = juce::jlimit( 0.0, 1.0, Normalised );
            applyCutoff();
        }

        void process( juce::AudioBuffer<float>& Buffer )
        {
            juce::dsp::AudioBlock<float>              block( Buffer );
            juce::dsp::ProcessContextReplacing<float> ctx( block );
            _filter.process( ctx );
        }

    private:
        void applyCutoff()
        {
            const double hz = freqRange.convertFrom0to1( _normalisedCutoff );
            _filter.setCutoffFrequency( (float)juce::jlimit( 20.0, juce::jmax( 21.0, _sampleRate * 0.49 ), hz ) );
        }

        juce::dsp::StateVariableTPTFilter<float> _filter;
        double                                   _sampleRate{ 48000 };
        double                                   _normalisedCutoff{ 1.0 };
    };

    //
    class SampleRecorder : public RecorderBase
    {
    public:
        explicit SampleRecorder( AudIO::Design& D )
            : RecorderBase( D )
        {
            setName( "SampleRecorder" );
        }
    };

    //
    class Looper : public RecorderBase
    {
    public:
        explicit Looper( AudIO::Design& D )
            : RecorderBase( D )
        {
            setName( "Looper" );
        }

        void  setMonitorGain( float Gain ) { _monitorGain = Gain; }
        float getMonitorGain() const { return _monitorGain; }

        void setRecord( bool Record ) { Record ? startRecording( true ) : stopRecording(); }
        void setPlay() { startPreview( 0 ); }

        bool isEmpty() const { return getLength() <= 0.0f; }
        bool isRunning() const { return isRecording() || isPreviewing(); }

        void    start( bool FromBeginning, bool /*SkipThresholdDetection*/ ) { startRecording( FromBeginning ); }
        void    play( double timeInSeconds ) { startPreview( timeInSeconds ); }
        void    stop() override { AudIO::AudioRecorder::stop(); stopPreview(); }
        void    clear() { reset(); }
        void    undo() {}
        void    redo() {}
        void    save( const juce::String& FilePath ) { saveRecording( FilePath.toStdString() ); }
        double& getProgress()
        {
            _progress = (double)readProgress();
            return _progress;
        }

    private:
        float  _monitorGain{ 1.0f };
        double _progress{ 0 };
    };

    //
    // JUCE-backed audio device. Feeds the engine (via IAudioDevice's
    // callback, set by AudIO::DeviceManager) from a juce::AudioDeviceManager.
    //
    class AudioDevice
        : public AudIO::IAudioDevice
        , private juce::AudioIODeviceCallback
    {
    public:
        AudioDevice( uint32_t nInputs, uint32_t nOutputs, const juce::String& deviceName )
            : AudIO::IAudioDevice( nInputs, nOutputs )
            , _numInputs( nInputs )
            , _numOutputs( nOutputs )
            , _deviceName( deviceName )
        {
        }

        ~AudioDevice() override { stop(); }

        juce::AudioDeviceManager& getJuceAudioDeviceMgr() { return _deviceManager; }

        void start( uint32_t samplerate, uint32_t blocksize ) override
        {
            // No sound card is not an error for our purposes: initialise()
            // returns an error string and no callbacks ever arrive.
            auto err = _deviceManager.initialise( (int)_numInputs, (int)_numOutputs, nullptr, true, _deviceName );
            juce::ignoreUnused( err );

            if( auto* dev = _deviceManager.getCurrentAudioDevice() )
            {
                auto setup        = _deviceManager.getAudioDeviceSetup();
                setup.sampleRate  = (double)samplerate;
                setup.bufferSize  = (int)blocksize;
                _deviceManager.setAudioDeviceSetup( setup, true );
            }

            prepareToPlay( samplerate, blocksize );
            _deviceManager.addAudioCallback( this );
            setState( AudIO::AudioDeviceState::stopped );
        }

        void resume() override { setState( AudIO::AudioDeviceState::playing ); }
        void pause() override { setState( AudIO::AudioDeviceState::paused ); }

        void stop() override
        {
            _deviceManager.removeAudioCallback( this );
            _deviceManager.closeAudioDevice();
            _buffer.clearOutputs();
            teardown();
            setState( AudIO::AudioDeviceState::stopped );
        }

    private:
        // JUCE 7.0.12: only the WithContext callback remains on
        // juce::AudioIODeviceCallback.
        void audioDeviceIOCallbackWithContext( const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels, int numSamples,
                                               const juce::AudioIODeviceCallbackContext& ) override
        {
            if( !isPlaying() )
            {
                for( int ch = 0; ch < numOutputChannels; ++ch )
                    if( outputChannelData[ch] )
                        std::fill_n( outputChannelData[ch], numSamples, 0.0f );
                return;
            }

            const int n = std::min( numSamples, (int)_buffer.getBlockSize() );
            for( uint32_t ch = 0; ch < _buffer.getNumInputChannels(); ++ch )
                if( auto* dst = _buffer.getInputBuffer( ch ) )
                {
                    if( ch < (uint32_t)numInputChannels && inputChannelData[ch] )
                        std::copy_n( inputChannelData[ch], n, dst );
                    else
                        std::fill_n( dst, n, 0.0f );
                }

            AudIO::IAudioDevice::process( _buffer );

            for( uint32_t ch = 0; ch < _buffer.getNumOutputChannels(); ++ch )
                if( ch < (uint32_t)numOutputChannels && outputChannelData[ch] )
                {
                    if( auto* src = _buffer.getOutputBuffer( ch ) )
                        std::copy_n( src, n, outputChannelData[ch] );
                    else
                        std::fill_n( outputChannelData[ch], n, 0.0f );
                }
        }

        void audioDeviceAboutToStart( juce::AudioIODevice* ) override {}
        void audioDeviceStopped() override {}

        juce::AudioDeviceManager _deviceManager;
        uint32_t                 _numInputs;
        uint32_t                 _numOutputs;
        juce::String             _deviceName;
    };
} // namespace mt
