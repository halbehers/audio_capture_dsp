#include "../../include/dsp/AudioCapture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace audiocapture::dsp
{

void AudioCapture::prepare(double targetSampleRate)
{
    _targetSampleRate = targetSampleRate;
    _lastSourceSampleRate = 0.0;
    _pendingCount = 0;
    _interpolatorLeft.reset();
    _interpolatorRight.reset();
    _fifo.reset();
    _latencyMonitor.reset();

    // See process()'s use of this flag: the device (especially an external/USB interface) may
    // take a while after this point to actually start delivering real audio callbacks, and the
    // producer keeps running the whole time - flush whatever accumulates during that gap too,
    // rather than delivering it as one already-late backlog on the first real process() call.
    //
    // Only do this if the pipeline was already live (blocks pushed before this prepare() call) -
    // i.e. this is a mid-capture device switch re-invoking prepare(), not the very first prepare()
    // before capture has started at all, where any backlog by the first process() call is
    // legitimate, expected startup latency rather than a device-switch artifact to discard.
    if (totalBlocksReceived.load() > 0)
        _flushBacklogOnNextProcess = true;
}

void AudioCapture::pushAudioBlock(const float* const* channelData, int numChannels, int numSamples, double sourceSampleRate)
{
    if (channelData == nullptr || numChannels <= 0 || numSamples <= 0 || _targetSampleRate <= 0.0)
        return;

    if (sourceSampleRate != _lastSourceSampleRate)
    {
        _lastSourceSampleRate = sourceSampleRate;
        _pendingCount = 0;
        _interpolatorLeft.reset();
        _interpolatorRight.reset();
    }

    const float* leftData = channelData[0];
    const float* rightData = numChannels > 1 ? channelData[1] : channelData[0]; // mono fallback: duplicate to R

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::abs(leftData[i]));
    lastWrittenBlockPeak = peak;

    int samplesToAppend = std::min(numSamples, maxPendingSamples - _pendingCount);
    std::memcpy(_pendingLeft + _pendingCount, leftData, (size_t) samplesToAppend * sizeof(float));
    std::memcpy(_pendingRight + _pendingCount, rightData, (size_t) samplesToAppend * sizeof(float));
    _pendingCount += samplesToAppend;

    double speedRatio = sourceSampleRate / _targetSampleRate; // input samples consumed per output sample
    lastSpeedRatio = speedRatio;
    lastPendingCountBeforeResample = _pendingCount;

    int numOutputSamples = (int) std::floor((double) _pendingCount / speedRatio);
    numOutputSamples = std::min(numOutputSamples, maxPendingSamples);
    lastNumOutputSamplesRequested = numOutputSamples;

    if (numOutputSamples <= 0)
        return;

    int usedLeft = _interpolatorLeft.process(speedRatio, _pendingLeft, _resampledLeft, numOutputSamples);
    int usedRight = _interpolatorRight.process(speedRatio, _pendingRight, _resampledRight, numOutputSamples);
    lastInterpolatorUsedLeft = usedLeft;
    lastInterpolatorUsedRight = usedRight;
    int used = std::max(usedLeft, usedRight);

    int remaining = _pendingCount - used;
    if (remaining > 0)
    {
        std::memmove(_pendingLeft, _pendingLeft + used, (size_t) remaining * sizeof(float));
        std::memmove(_pendingRight, _pendingRight + used, (size_t) remaining * sizeof(float));
    }
    _pendingCount = remaining;

    lastFifoFreeSpaceBeforeWrite = _fifo.getFreeSpace();

    int start1, size1, start2, size2;
    _fifo.prepareToWrite(numOutputSamples, start1, size1, start2, size2);
    lastFifoWriteSize1 = size1;
    lastFifoWriteSize2 = size2;

    auto writeChunk = [&](int destStart, int srcOffset, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            _fifoBuffer.setSample(0, destStart + i, _resampledLeft[srcOffset + i]);
            _fifoBuffer.setSample(1, destStart + i, _resampledRight[srcOffset + i]);
        }
    };

    writeChunk(start1, 0, size1);
    if (size2 > 0)
        writeChunk(start2, size1, size2);

    _fifo.finishedWrite(size1 + size2);
    totalBlocksReceived++;
    _latencyMonitor.recordProduced(size1 + size2);
}

int AudioCapture::process(juce::AudioBuffer<float>& destBuffer)
{
    totalProcessCalls++;
    lastRequestedBufferSize = destBuffer.getNumSamples();

    if (_flushBacklogOnNextProcess)
    {
        _flushBacklogOnNextProcess = false;

        int flushStart1, flushSize1, flushStart2, flushSize2;
        _fifo.prepareToRead(_fifo.getNumReady(), flushStart1, flushSize1, flushStart2, flushSize2);
        _fifo.finishedRead(flushSize1 + flushSize2);
        _latencyMonitor.reset();
    }

    int numToRead = juce::jmin(_fifo.getNumReady(), destBuffer.getNumSamples());
    lastNumRead = numToRead;

    int start1, size1, start2, size2;
    _fifo.prepareToRead(numToRead, start1, size1, start2, size2);

    auto copyChunk = [&](int destOffset, int srcStart, int n)
    {
        for (int ch = 0; ch < destBuffer.getNumChannels(); ++ch)
            destBuffer.copyFrom(ch, destOffset, _fifoBuffer, juce::jmin(ch, 1), srcStart, n);
    };

    copyChunk(0, start1, size1);
    if (size2 > 0)
        copyChunk(size1, start2, size2);

    _fifo.finishedRead(size1 + size2);
    totalSamplesRead += (size1 + size2);
    _currentLatencyMs.store(_latencyMonitor.consumeLatencyMs(size1 + size2));

    if (destBuffer.getNumChannels() > 0)
        lastReadBlockPeak = destBuffer.getMagnitude(0, 0, destBuffer.getNumSamples());

    // if numToRead < destBuffer.getNumSamples(), the rest stays whatever destBuffer already held
    // - underrun handling (e.g. silencing the tail) is the caller's responsibility.
    return numToRead;
}

}
