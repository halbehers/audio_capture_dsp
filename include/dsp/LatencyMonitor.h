#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

namespace audiocapture::dsp
{

// Lock-free, single-producer/single-consumer dwell-time tracker: the producer records how many
// units it has produced so far, the consumer reports how many it has just consumed, and the
// monitor returns how long (in ms) those units sat in the pipeline between the two. Not audio- or
// sample-specific - "units" can be samples, frames, bytes, or any other producer/consumer count.
class LatencyMonitor
{
public:
    void recordProduced(juce::int64 unitsProduced);

    double consumeLatencyMs(juce::int64 unitsConsumed);

private:
    struct Capture
    {
        juce::int64 cumulativeUnitsProduced = 0;
        double captureTimeMs = 0.0;
    };

    static constexpr int ringCapacity = 256; // power of 2 - index wraps via bitmask

    std::array<Capture, ringCapacity> _ring {};
    std::atomic<int> _writeIndex { -1 }; // -1 == "nothing recorded yet"

    juce::int64 _cumulativeUnitsProduced = 0;

    juce::int64 _cumulativeUnitsConsumed = 0;
    int _readCursor = 0;
};

}
