#include "../../include/capture/AudioOutputDeviceList.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace audiocapture
{

std::vector<AudioOutputDeviceInfo> AudioOutputDeviceList::getAllDevices()
{
    std::vector<AudioOutputDeviceInfo> result;

    AudioOutputDeviceInfo noDevice;
    noDevice.kind = AudioOutputDeviceKind::noDevice;
    result.push_back(noDevice);

    AudioOutputDeviceInfo systemDefault;
    systemDefault.kind = AudioOutputDeviceKind::systemDefault;
    result.push_back(systemDefault);

    // Throwaway AudioDeviceManager, only used for createAudioDeviceTypes() - never initialise()'d,
    // so this never opens/starts an actual audio device.
    juce::AudioDeviceManager deviceManager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    deviceManager.createAudioDeviceTypes(types);

    for (auto* type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices(); // required before getDeviceNames()/getDefaultDeviceIndex() below

        juce::StringArray outputNames = type->getDeviceNames(false); // wantInputNames=false -> output-capable
        juce::StringArray inputNames  = type->getDeviceNames(true);

        int defaultOutputIndex = type->getDefaultDeviceIndex(false); // index into outputNames, see header

        for (int i = 0; i < outputNames.size(); ++i)
        {
            const juce::String& deviceName = outputNames[i];

            // Duplex devices (e.g. a virtual routing device) report the identical name in both
            // the input- and output-capable name lists; pass it as both outputDeviceName and
            // inputDeviceName so the single created AudioIODevice reports both directions'
            // channel counts. Simplex output-only devices get an empty inputDeviceName.
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
            info.kind = AudioOutputDeviceKind::device;
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
        case AudioOutputDeviceKind::noDevice:      return "No Device";
        case AudioOutputDeviceKind::systemDefault: return "Use System Device";
        case AudioOutputDeviceKind::device:        break;
    }

    return device.name + " (" + juce::String(device.numInputChannels) + " In, "
                              + juce::String(device.numOutputChannels) + " Out)";
}

}
