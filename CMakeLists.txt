cmake_minimum_required(VERSION 3.22)

project(audio_capture_dsp_tests VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_VERSION 8.0.14)
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libs")
set(CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# This file exists so this repo is independently buildable/testable on its own - but when fetched
# as a dependency by a consuming project (e.g. via CPM + juce_add_module(), as app_in_daw does),
# that project already calls juce_add_module() on this same source directory itself, so doing it
# again here too collides ("add_library cannot create target ... because another target with the
# same name already exists"). Only self-register when this really is the top-level CMake project
# being configured, i.e. someone is building/testing this repo standalone.
if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif ()

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
