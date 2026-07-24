cmake_minimum_required(VERSION 3.22)

project(audio_capture_dsp_tests VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_VERSION 8.0.14)
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libs")
set(CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(${CMAKE_DIR}/cpm.cmake)

cpmaddpackage(
        NAME JUCE
        GIT_TAG ${JUCE_VERSION}
        VERSION ${JUCE_VERSION}
        GITHUB_REPOSITORY juce-framework/JUCE
        SOURCE_DIR ${LIB_DIR}/juce
)

# Registers this repo's own module root (audio_capture_dsp.h/.cpp/.mm) exactly as a consuming
# project would via juce_add_module() - this proves the module declaration and unity .mm facade
# actually compile/link, as a side effect of building the test suite.
juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}" ALIAS_NAMESPACE audiocapture)

cpmaddpackage(
        NAME CATCH2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.7.1
)

enable_testing()
add_subdirectory(Tests)
