#include "../../include/capture/SystemAudioTap.h"

#if JUCE_MAC

#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#endif

namespace audiocapture
{

struct SystemAudioTap::Impl
{
#if JUCE_MAC
    AudioObjectID processObjectID = kAudioObjectUnknown;
    AudioObjectID tapObjectID = kAudioObjectUnknown;
    AudioObjectID aggregateDeviceID = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProcID = nullptr;
    CATapDescription* tapDescription = nil;
    double tapSampleRate = 48000.0;
    bool tapIsInterleaved = false;

    static constexpr int maxFramesPerCallback = 1 << 14;
    float deinterleavedLeft[maxFramesPerCallback];
    float deinterleavedRight[maxFramesPerCallback];
#endif

    AudioCallback callback;
    bool running = false;
};

SystemAudioTap::SystemAudioTap() : _impl(std::make_unique<Impl>()) {}

SystemAudioTap::~SystemAudioTap()
{
    stop();
}

bool SystemAudioTap::isSupported()
{
#if JUCE_MAC
    if (@available(macOS 14.2, *))
        return true;
#endif
    return false;
}

bool SystemAudioTap::isRunning() const
{
    return _impl->running;
}

void SystemAudioTap::stop()
{
#if JUCE_MAC
    if (! _impl->running)
        return;

    if (_impl->aggregateDeviceID != kAudioObjectUnknown && _impl->ioProcID != nullptr)
    {
        AudioDeviceStop(_impl->aggregateDeviceID, _impl->ioProcID);
        AudioDeviceDestroyIOProcID(_impl->aggregateDeviceID, _impl->ioProcID);
    }
    _impl->ioProcID = nullptr;

    if (_impl->aggregateDeviceID != kAudioObjectUnknown)
    {
        AudioHardwareDestroyAggregateDevice(_impl->aggregateDeviceID);
        _impl->aggregateDeviceID = kAudioObjectUnknown;
    }

    if (_impl->tapObjectID != kAudioObjectUnknown)
    {
        AudioHardwareDestroyProcessTap(_impl->tapObjectID);
        _impl->tapObjectID = kAudioObjectUnknown;
    }

    _impl->tapDescription = nil;
    _impl->processObjectID = kAudioObjectUnknown;
    _impl->callback = nullptr;
    _impl->running = false;
#endif
}

#if JUCE_MAC

namespace
{
    bool translatePIDToProcessObject(pid_t pid, AudioObjectID& outProcessObjectID)
    {
        AudioObjectPropertyAddress address {
            kAudioHardwarePropertyTranslatePIDToProcessObject,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        AudioObjectID processObjectID = kAudioObjectUnknown;
        UInt32 dataSize = sizeof(processObjectID);

        OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(pid), &pid, &dataSize, &processObjectID);

        if (status != noErr || processObjectID == kAudioObjectUnknown)
        {
            DBG("SystemAudioTap: could not translate pid " << (int) pid << " to a process object (status=" << (int) status << ")");
            return false;
        }

        outProcessObjectID = processObjectID;
        return true;
    }
}

#endif

bool SystemAudioTap::start(int targetProcessID, AudioCallback callback)
{
    stop();

#if JUCE_MAC
    if (! isSupported())
    {
        DBG("SystemAudioTap: unsupported OS (requires macOS 14.2+)");
        return false;
    }

    AudioObjectID processObjectID = kAudioObjectUnknown;
    if (! translatePIDToProcessObject((pid_t) targetProcessID, processObjectID))
        return false;

    CATapDescription* tapDescription = [[CATapDescription alloc] initStereoMixdownOfProcesses: @[ @(processObjectID) ]];
    tapDescription.UUID = [NSUUID UUID];
    tapDescription.privateTap = YES;

    tapDescription.muteBehavior = CATapMuted;

    AudioObjectID tapObjectID = kAudioObjectUnknown;
    OSStatus status = AudioHardwareCreateProcessTap(tapDescription, &tapObjectID);
    if (status != noErr || tapObjectID == kAudioObjectUnknown)
    {
        DBG("SystemAudioTap: AudioHardwareCreateProcessTap failed (status=" << (int) status << ")");
        return false;
    }

    NSDictionary* subTapDescription = @{
        @(kAudioSubTapUIDKey): tapDescription.UUID.UUIDString,
        @(kAudioSubTapDriftCompensationKey): @NO
    };

    NSDictionary* aggregateDeviceDescription = @{
        @(kAudioAggregateDeviceNameKey): @"AudioCaptureDSP-Capture",
        @(kAudioAggregateDeviceUIDKey): [[NSUUID UUID] UUIDString],
        @(kAudioAggregateDeviceIsPrivateKey): @YES,
        @(kAudioAggregateDeviceTapListKey): @[ subTapDescription ]
    };

    AudioObjectID aggregateDeviceID = kAudioObjectUnknown;
    status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef) aggregateDeviceDescription, &aggregateDeviceID);
    if (status != noErr || aggregateDeviceID == kAudioObjectUnknown)
    {
        DBG("SystemAudioTap: AudioHardwareCreateAggregateDevice failed (status=" << (int) status << ")");
        AudioHardwareDestroyProcessTap(tapObjectID);
        return false;
    }

    AudioStreamBasicDescription tapFormat {};
    UInt32 tapFormatSize = sizeof(tapFormat);
    AudioObjectPropertyAddress tapFormatAddress {
        kAudioTapPropertyFormat,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectGetPropertyData(tapObjectID, &tapFormatAddress, 0, nullptr, &tapFormatSize, &tapFormat) == noErr
        && tapFormat.mSampleRate > 0.0)
    {
        _impl->tapSampleRate = tapFormat.mSampleRate;
        _impl->tapIsInterleaved = (tapFormat.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0;

        DBG("SystemAudioTap: tap format sampleRate=" << tapFormat.mSampleRate
            << " channelsPerFrame=" << (int) tapFormat.mChannelsPerFrame
            << " interleaved=" << (_impl->tapIsInterleaved ? "yes" : "no")
            << " bitsPerChannel=" << (int) tapFormat.mBitsPerChannel);
    }
    else
    {
        DBG("SystemAudioTap: failed to read tap format, assuming " << _impl->tapSampleRate << "Hz non-interleaved stereo");
    }

    _impl->processObjectID = processObjectID;
    _impl->tapDescription = tapDescription;
    _impl->tapObjectID = tapObjectID;
    _impl->aggregateDeviceID = aggregateDeviceID;
    _impl->callback = std::move(callback);

    AudioDeviceIOProcID ioProcID = nullptr;
    Impl* impl = _impl.get();

    status = AudioDeviceCreateIOProcIDWithBlock(&ioProcID, aggregateDeviceID, nullptr,
        ^(const AudioTimeStamp*, const AudioBufferList* inInputData, const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*)
        {
            if (inInputData == nullptr || inInputData->mNumberBuffers == 0 || ! impl->callback)
                return;

            if (impl->tapIsInterleaved)
            {
                const auto& buffer = inInputData->mBuffers[0];
                int numFrames = (int) (buffer.mDataByteSize / (sizeof(float) * 2));
                numFrames = juce::jmin(numFrames, Impl::maxFramesPerCallback);

                const auto* interleaved = (const float*) buffer.mData;
                for (int i = 0; i < numFrames; ++i)
                {
                    impl->deinterleavedLeft[i] = interleaved[i * 2];
                    impl->deinterleavedRight[i] = interleaved[i * 2 + 1];
                }

                const float* channelData[2] { impl->deinterleavedLeft, impl->deinterleavedRight };
                impl->callback(channelData, 2, numFrames, impl->tapSampleRate);
            }
            else
            {
                constexpr UInt32 maxChannels = 8;
                const float* channelData[maxChannels];
                UInt32 numChannels = juce::jmin(inInputData->mNumberBuffers, maxChannels);

                for (UInt32 i = 0; i < numChannels; ++i)
                    channelData[i] = (const float*) inInputData->mBuffers[i].mData;

                int numSamples = (int) (inInputData->mBuffers[0].mDataByteSize / sizeof(float));

                impl->callback(channelData, (int) numChannels, numSamples, impl->tapSampleRate);
            }
        });

    if (status != noErr || ioProcID == nullptr)
    {
        DBG("SystemAudioTap: AudioDeviceCreateIOProcIDWithBlock failed (status=" << (int) status << ")");
        AudioHardwareDestroyAggregateDevice(aggregateDeviceID);
        AudioHardwareDestroyProcessTap(tapObjectID);
        _impl->aggregateDeviceID = kAudioObjectUnknown;
        _impl->tapObjectID = kAudioObjectUnknown;
        _impl->callback = nullptr;
        return false;
    }

    _impl->ioProcID = ioProcID;

    status = AudioDeviceStart(aggregateDeviceID, ioProcID);
    if (status != noErr)
    {
        DBG("SystemAudioTap: AudioDeviceStart failed (status=" << (int) status << ")");
        AudioDeviceDestroyIOProcID(aggregateDeviceID, ioProcID);
        AudioHardwareDestroyAggregateDevice(aggregateDeviceID);
        AudioHardwareDestroyProcessTap(tapObjectID);
        _impl->ioProcID = nullptr;
        _impl->aggregateDeviceID = kAudioObjectUnknown;
        _impl->tapObjectID = kAudioObjectUnknown;
        _impl->callback = nullptr;
        return false;
    }

    _impl->running = true;
    return true;
#else
    juce::ignoreUnused(targetProcessID, callback);
    return false;
#endif
}

}
