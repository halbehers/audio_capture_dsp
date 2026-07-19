#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace audiocapture
{

// Taps the audio output of an arbitrary OS process.
// Not tied to any particular target process or product - give it a process ID and
// a callback, and it forwards deinterleaved float audio blocks as they arrive.
class SystemAudioTap
{
public:
    using AudioCallback = std::function<void(const float* const* channelData, int numChannels, int numSamples, double sampleRate)>;

    SystemAudioTap();
    ~SystemAudioTap();

    static bool isSupported();

    bool start(int targetProcessID, AudioCallback callback);

    void stop();

    bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemAudioTap)
};

}
