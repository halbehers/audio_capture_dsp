#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

TEST_CASE("AudioOutputDeviceList::getDisplayName for NO_DEVICE", "[AudioOutputDeviceList]")
{
    audiocapture::AudioOutputDeviceInfo device;
    device.kind = audiocapture::AudioOutputDeviceKind::NO_DEVICE;

    CHECK(audiocapture::AudioOutputDeviceList::getDisplayName(device) == "No Device");
}

TEST_CASE("AudioOutputDeviceList::getDisplayName for SYSTEM_DEFAULT", "[AudioOutputDeviceList]")
{
    audiocapture::AudioOutputDeviceInfo device;
    device.kind = audiocapture::AudioOutputDeviceKind::SYSTEM_DEFAULT;

    CHECK(audiocapture::AudioOutputDeviceList::getDisplayName(device) == "Use System Device");
}

TEST_CASE("AudioOutputDeviceList::getDisplayName for a typical stereo DEVICE", "[AudioOutputDeviceList]")
{
    audiocapture::AudioOutputDeviceInfo device;
    device.kind = audiocapture::AudioOutputDeviceKind::DEVICE;
    device.name = "Scarlett 2i2";
    device.numInputChannels = 2;
    device.numOutputChannels = 2;

    CHECK(audiocapture::AudioOutputDeviceList::getDisplayName(device) == "Scarlett 2i2 (2 In, 2 Out)");
}

TEST_CASE("AudioOutputDeviceList::getDisplayName for an output-only DEVICE", "[AudioOutputDeviceList]")
{
    audiocapture::AudioOutputDeviceInfo device;
    device.kind = audiocapture::AudioOutputDeviceKind::DEVICE;
    device.name = "Studio Monitors";
    device.numInputChannels = 0;
    device.numOutputChannels = 4;

    CHECK(audiocapture::AudioOutputDeviceList::getDisplayName(device) == "Studio Monitors (0 In, 4 Out)");
}

TEST_CASE("AudioOutputDeviceList::getDisplayName for DEVICE ignores typeName and isDefaultOutputDevice", "[AudioOutputDeviceList]")
{
    audiocapture::AudioOutputDeviceInfo deviceA;
    deviceA.kind = audiocapture::AudioOutputDeviceKind::DEVICE;
    deviceA.name = "Interface";
    deviceA.typeName = "CoreAudio";
    deviceA.numInputChannels = 2;
    deviceA.numOutputChannels = 2;
    deviceA.isDefaultOutputDevice = false;

    audiocapture::AudioOutputDeviceInfo deviceB = deviceA;
    deviceB.typeName = "JACK";
    deviceB.isDefaultOutputDevice = true;

    // Regression pin: a later change that starts folding typeName/isDefaultOutputDevice into the
    // label would silently break this contract.
    CHECK(audiocapture::AudioOutputDeviceList::getDisplayName(deviceA) == audiocapture::AudioOutputDeviceList::getDisplayName(deviceB));
}

TEST_CASE("AudioOutputDeviceList::getAllDevices prepends NO_DEVICE then SYSTEM_DEFAULT sentinels", "[AudioOutputDeviceList]")
{
    // Requires the JUCE message thread - guaranteed by main.cpp's ScopedJuceInitialiser_GUI.
    const auto devices = audiocapture::AudioOutputDeviceList::getAllDevices();

    REQUIRE(devices.size() >= 2);
    CHECK(devices[0].kind == audiocapture::AudioOutputDeviceKind::NO_DEVICE);
    CHECK(devices[1].kind == audiocapture::AudioOutputDeviceKind::SYSTEM_DEFAULT);

    // No assertions about which real devices are present (hardware-dependent, varies per CI
    // runner) - only the structural sentinel-order contract and that every real device entry has
    // a sane channel count.
    for (size_t i = 2; i < devices.size(); ++i)
    {
        INFO("device index " << i);
        CHECK(devices[i].kind == audiocapture::AudioOutputDeviceKind::DEVICE);
        CHECK(devices[i].numOutputChannels > 0);
    }
}
