#pragma once

#include <juce_events/juce_events.h>

#include "SystemAudioTap.h"
#include "../dsp/AudioCapture.h"

namespace audiocapture
{

// Reusable retry/lifecycle glue around SystemAudioTap: polls on a timer until a target process ID
// resolves, starts the tap once it does, retries a bounded number of times, and forwards captured
// audio into the destination AudioCapture. Subclasses only need to supply *how* to find the
// target process ID (e.g. by locating some OS window/component's owning process).
class ProcessAudioCapture : private juce::Timer
{
public:
    explicit ProcessAudioCapture(dsp::AudioCapture& destinationCapture);
    ~ProcessAudioCapture() override;

    void startCapture();

    void stopCapture();

protected:
    virtual bool getProcessID(int& outProcessID) = 0;

private:
    void timerCallback() override;

    dsp::AudioCapture& _destinationCapture;
    SystemAudioTap _tap;
    int _retryCount = 0;

    static constexpr int maxRetries = 60; // ~15s at the 250ms poll interval below
    static constexpr int retryIntervalMs = 250;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessAudioCapture)
};

}
