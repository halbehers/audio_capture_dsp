add_executable(audio_capture_dsp_tests
        dsp/LatencyMonitorTests.cpp
        dsp/AudioCaptureTests.cpp
        capture/SystemAudioTapTests.cpp
        capture/ProcessAudioCaptureTests.cpp
        capture/ProcessListTests.cpp
        capture/AudioOutputDeviceListTests.cpp
        main.cpp
)

target_link_libraries(audio_capture_dsp_tests PRIVATE
        audiocapture::audio_capture_dsp
        juce::juce_gui_basics
        Catch2::Catch2
        juce::juce_recommended_warning_flags
)

# ProcessAudioCaptureTests.cpp needs runDispatchLoopUntil() to pump timer callbacks synchronously
# in a headless console app - that method is compiled out unless this is set.
target_compile_definitions(audio_capture_dsp_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

# Catch2's CMake extras (Catch.cmake, providing catch_discover_tests) aren't on CMAKE_MODULE_PATH
# automatically when Catch2 is fetched via CPM as source, unlike an installed package.
list(APPEND CMAKE_MODULE_PATH "${CPM_PACKAGE_CATCH2_SOURCE_DIR}/extras")

include(CTest)
include(Catch)
catch_discover_tests(audio_capture_dsp_tests)
