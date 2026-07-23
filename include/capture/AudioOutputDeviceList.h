#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace audiocapture
{

enum class AudioOutputDeviceKind
{
    NO_DEVICE,
    SYSTEM_DEFAULT,
    DEVICE
};

struct AudioOutputDeviceInfo
{
    AudioOutputDeviceKind kind = AudioOutputDeviceKind::NO_DEVICE;

    juce::String name;

    juce::String typeName;

    int numInputChannels = 0;
    int numOutputChannels = 0;

    bool isDefaultOutputDevice = false;
};

class AudioOutputDeviceList
{
public:
    static std::vector<AudioOutputDeviceInfo> getAllDevices();

    static juce::String getDisplayName(const AudioOutputDeviceInfo& device);
};

}
