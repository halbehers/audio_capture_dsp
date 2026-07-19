#include "../../include/capture/ProcessAudioCapture.h"

namespace audiocapture
{

ProcessAudioCapture::ProcessAudioCapture(dsp::AudioCapture& destinationCapture)
    : _destinationCapture(destinationCapture)
{
}

ProcessAudioCapture::~ProcessAudioCapture()
{
    stopCapture();
}

void ProcessAudioCapture::startCapture()
{
    if (! SystemAudioTap::isSupported())
    {
        DBG("ProcessAudioCapture: system audio capture unsupported on this OS (requires macOS 14.2+)");
        return;
    }

    _tap.stop();
    _retryCount = 0;
    startTimer(retryIntervalMs);
}

void ProcessAudioCapture::stopCapture()
{
    stopTimer();
    _tap.stop();
}

void ProcessAudioCapture::timerCallback()
{
    int processID = 0;
    if (getProcessID(processID))
    {
        bool started = _tap.start(processID, [this](const float* const* channelData, int numChannels, int numSamples, double sampleRate)
        {
            _destinationCapture.pushAudioBlock(channelData, numChannels, numSamples, sampleRate);
        });

        if (started)
        {
            stopTimer();
            return;
        }
    }

    if (++_retryCount >= maxRetries)
    {
        stopTimer();
        DBG("ProcessAudioCapture: gave up starting SystemAudioTap after " << maxRetries << " attempts");
    }
}

}
