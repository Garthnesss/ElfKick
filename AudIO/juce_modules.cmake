# Reconstruction (not in the public release): the private AudIO repo shipped this
# file; Kick/3rdParty.cmake includes it after fetching AudIO.
juce_add_modules(${CMAKE_CURRENT_LIST_DIR}/AudIO)
juce_add_modules(${CMAKE_CURRENT_LIST_DIR}/AudIO_constants)
juce_add_modules(${CMAKE_CURRENT_LIST_DIR}/AudIO_juce)
