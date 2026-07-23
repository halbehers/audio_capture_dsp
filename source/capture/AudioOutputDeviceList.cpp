#include "../../include/capture/AudioOutputDeviceList.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace audiocapture
{

std::vector<AudioOutputDeviceInfo> AudioOutputDeviceList::getAllDevices()
{
    std::vector<AudioOutputDeviceInfo> result;

    AudioOutputDeviceInfo noDevice;
    noDevice.kind = AudioOutputDeviceKind::NO_DEVICE;
    result.push_back(noDevice);

    AudioOutputDeviceInfo systemDefault;
    systemDefault.kind = AudioOutputDeviceKind::SYSTEM_DEFAULT;
    result.push_back(systemDefault);

    juce::AudioDeviceManager deviceManager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    deviceManager.createAudioDeviceTypes(types);

    for (auto* type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();

        juce::StringArray outputNames = type->getDeviceNames(false);
        juce::StringArray inputNames  = type->getDeviceNames(true);

        int defaultOutputIndex = type->getDefaultDeviceIndex(false);

        for (int i = 0; i < outputNames.size(); ++i)
        {
            const juce::String& deviceName = outputNames[i];

            bool isDuplex = inputNames.contains(deviceName);
            std::unique_ptr<juce::AudioIODevice> device(
                type->createDevice(deviceName, isDuplex ? deviceName : juce::String()));

            if (device == nullptr)
                continue; // driver failed to hand back a device (e.g. unplugged mid-scan) - skip it

            int numOutputChannels = device->getOutputChannelNames().size();
            int numInputChannels  = device->getInputChannelNames().size();

            if (numOutputChannels <= 0)
                continue; // listed as output-capable but reports zero actual output channels - skip

            AudioOutputDeviceInfo info;
            info.kind = AudioOutputDeviceKind::DEVICE;
            info.name = deviceName;
            info.typeName = type->getTypeName();
            info.numInputChannels = numInputChannels;
            info.numOutputChannels = numOutputChannels;
            info.isDefaultOutputDevice = (i == defaultOutputIndex);

            result.push_back(std::move(info));
        }
    }

    return result;
}

juce::String AudioOutputDeviceList::getDisplayName(const AudioOutputDeviceInfo& device)
{
    switch (device.kind)
    {
        case AudioOutputDeviceKind::NO_DEVICE:      return "No Device";
        case AudioOutputDeviceKind::SYSTEM_DEFAULT: return "Use System Device";
        case AudioOutputDeviceKind::DEVICE:         break;
    }

    return device.name + " (" + juce::String(device.numInputChannels) + " In, "
                              + juce::String(device.numOutputChannels) + " Out)";
}

}
