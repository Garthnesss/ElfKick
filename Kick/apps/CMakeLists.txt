# Main application.  
add_subdirectory(kick)
add_subdirectory(kick_device_control)
add_subdirectory(kick_gui)
#add_subdirectory(kick_logger)
add_subdirectory(kick_audio_engine)
add_subdirectory(kick_main)
add_subdirectory(kick_audio_model)
add_subdirectory(synth_harness)
# ^ ElfKick addition: offline synth render harness (SYNTH_DESIGN.md Step 2)

set(KICK_TESTS  False)

if (KICK_TESTS)
    add_subdirectory(kick_device_control_test)
endif()

if (KICK_PLAYBACK)
    add_subdirectory(kick_audio)
endif()

# Software Simulator
if( KICK_SIMULATOR )
    add_subdirectory(kickem)
endif()

# KICK VST plugin
if( KICK_VST_PLUGIN )
    add_subdirectory(kickvst)
endif()
# KICK MIDI PORT Publish
if( MIDIPORT )
	add_subdirectory(midiPort)
endif()

if(KICK_MULTITOUCH)
   add_subdirectory(multitouch)
endif()
