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

    // Describes why the most recent start() attempt failed (or why isRunning() became false),
    // empty if the last attempt succeeded or none has been made yet. Every backend also still logs
    // the same information via DBG() for local/attached-debugger use; this exists so callers
    // embedded in a process nobody can attach a debugger to (e.g. a plugin loaded in a DAW) have
    // some way to surface *why* capture isn't working, without this module taking on a dependency
    // on any particular product's own logging.
    [[nodiscard]] juce::String getLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemAudioTap)
};

}
