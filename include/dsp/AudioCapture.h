#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "LatencyMonitor.h"

#include <atomic>

namespace audiocapture::dsp
{

// Generic resampling producer/consumer audio-capture buffer: accepts audio blocks at an
// arbitrary, possibly-changing source sample rate on one thread (pushAudioBlock, e.g. an OS audio
// tap callback), and drains them at a fixed target sample rate on another (process, e.g. a
// plugin's processBlock). Tracks producer->consumer dwell time via a LatencyMonitor.
class AudioCapture
{
public:
    void prepare(double targetSampleRate);

    // Producer side. May be called from any thread (e.g. an OS audio-tap callback thread).
    void pushAudioBlock(const float* const* channelData, int numChannels, int numSamples, double sourceSampleRate);

    // Consumer side. Drains as many ready samples as fit into destBuffer (starting at sample 0),
    // up-mixing to destBuffer's channel count. Returns the number of samples written; any
    // remaining tail of destBuffer beyond that is left untouched (the caller's underrun to
    // handle). Also updates getCurrentLatencyMs().
    int process(juce::AudioBuffer<float>& destBuffer);

    double getCurrentLatencyMs() const { return _currentLatencyMs.load(); }

    // Diagnostics.
    std::atomic<int> totalBlocksReceived { 0 };
    std::atomic<int> totalProcessCalls { 0 };
    std::atomic<int> totalSamplesRead { 0 };
    std::atomic<int> lastNumRead { 0 };
    std::atomic<int> lastRequestedBufferSize { 0 };
    std::atomic<float> lastWrittenBlockPeak { 0.0f };
    std::atomic<float> lastReadBlockPeak { 0.0f };

private:
    static constexpr int fifoCapacity = 1 << 16;
    static constexpr int maxPendingSamples = 1 << 14;

    juce::AbstractFifo _fifo { fifoCapacity };
    juce::AudioBuffer<float> _fifoBuffer { 2, fifoCapacity }; // stereo scratch storage

    LatencyMonitor _latencyMonitor;
    std::atomic<double> _currentLatencyMs { 0.0 };

    double _targetSampleRate = 44100.0;
    double _lastSourceSampleRate = 0.0;

    juce::LagrangeInterpolator _interpolatorLeft, _interpolatorRight;
    float _pendingLeft[maxPendingSamples];
    float _pendingRight[maxPendingSamples];
    int _pendingCount = 0;

    float _resampledLeft[maxPendingSamples];
    float _resampledRight[maxPendingSamples];
};

}
