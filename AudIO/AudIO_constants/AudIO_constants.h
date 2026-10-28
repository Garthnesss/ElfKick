#pragma once
/*
  BEGIN_JUCE_MODULE_DECLARATION
  ID:                 AudIO_constants
  vendor:             MUSIC Tribe, MY
  version:            0.1.0
  name:               MusicTribe AudIO constants
  description:        Constants shared between AudIO and Kick (RECONSTRUCTED - not in public release)
  license:            AGPLv3
  dependencies:       juce_core
  END_JUCE_MODULE_DECLARATION
*/

/*
  RECONSTRUCTED (ElfKick, Oct 2026): the private AudIO repo's constants were
  not part of the public release. The namespaces below were rebuilt from the
  call sites in the Kick tree (modules/common, modules/engine) following the
  same global-namespace / static constexpr style as Kick's own
  modules/common/source/kick_Constants.h (BPM, BAR, ...). Values are chosen
  to be consistent with every use found (template arguments, ValueTree
  defaults, meter math); they are inference, not upstream gospel.
*/

namespace MIDI
{
    static constexpr int   MIN{ 0 };
    static constexpr int   MAX{ 127 };
    static constexpr int   STEP{ 1 };
    static constexpr int   DEFAULT{ 0 };
    static constexpr int   NUMERIC_MAX{ 128 };
    static constexpr int   MIDDLE_C{ 60 };
    static constexpr int   LEVEL_DEFAULT{ 100 };
    static constexpr int   NOTE_DEFAULT{ 60 };
    static constexpr float SCALER{ 1.0f / 127.0f };
}

namespace MIDI_CH
{
    static constexpr int MIN{ 1 };
    static constexpr int MAX{ 16 };
    static constexpr int DEFAULT{ 1 };
}

namespace DB
{
    static constexpr float MIN{ -100.0f };
}

namespace GAIN
{
    static constexpr double MIN{ 0.0 };
    static constexpr double MAX{ 2.0 };
    static constexpr double STEP{ 0.01 };
    static constexpr double DEFAULT{ 1.0 };
    static constexpr double ROOT_2{ 1.41421356 };
}

namespace PAN
{
    static constexpr double MIN{ -1.0 };
    static constexpr double MAX{ 1.0 };
    static constexpr double STEP{ 0.01 };
    static constexpr double DEFAULT{ 0.0 };
}

namespace POLYPHONY
{
    static constexpr int MIN{ 1 };
    static constexpr int MAX{ 16 };
    static constexpr int STEP{ 1 };
    static constexpr int DEFAULT{ 16 };
}

// RECONSTRUCTED: sentinel "no value" marker (used as juce::var(UNKNOWN_VALUE)).
static constexpr int UNKNOWN_VALUE{ -9999 };

// RECONSTRUCTED: device/engine geometry, matching AudIO's own examples
// (48000 Hz, 960-sample blocks) and the DawEngine constructor arguments in
// modules/engine/audio_engine/engine/mt_audio_manager.h.
static constexpr int AUDIO_SAMPLE_RATE{ 48000 };
static constexpr int AUDIO_BLOCK_SIZE{ 960 };
static constexpr int AUDIO_NUM_INPUTS{ 2 };
static constexpr int AUDIO_NUM_OUTPUTS{ 2 };
static constexpr int AUDIO_NUM_SEND_RETURNS{ 4 };
static constexpr int AUDIO_NUM_SUBMIX{ 0 };
static constexpr int AUDIO_NUM_AUX{ 0 };

namespace AudIO
{
    namespace Constants
    {
    }
}
