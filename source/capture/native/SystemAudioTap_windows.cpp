#include "../../../include/capture/SystemAudioTap.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <wrl/client.h>
#include <wrl/implements.h>

namespace audiocapture
{

namespace
{
    using Microsoft::WRL::ClassicCom;
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::FtmBase;
    using Microsoft::WRL::RuntimeClass;
    using Microsoft::WRL::RuntimeClassFlags;

    // Bridges ActivateAudioInterfaceAsync's callback-based completion back into the synchronous
    // start() contract every platform in this module follows. FtmBase marks this free-threaded so
    // COM doesn't try to marshal the callback across apartments - ActivateCompleted() may
    // legitimately fire on an arbitrary MTA worker thread while start() is still blocked waiting
    // on completedEvent below.
    class ActivationCompletionHandler
        : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler>
    {
    public:
        HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override
        {
            IUnknown* activatedInterface = nullptr;
            HRESULT activationResult = E_FAIL;

            operation->GetActivateResult(&activationResult, &activatedInterface);

            result = activationResult;
            if (SUCCEEDED(result) && activatedInterface != nullptr)
                activatedInterface->QueryInterface(IID_PPV_ARGS(&audioClient));

            if (activatedInterface != nullptr)
                activatedInterface->Release();

            SetEvent(completedEvent);
            return S_OK;
        }

        HANDLE completedEvent = nullptr;
        HRESULT result = E_FAIL;
        IAudioClient* audioClient = nullptr;
    };

}

struct SystemAudioTap::Impl
{
    static constexpr int maxFramesPerCallback = 1 << 14;

    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    HANDLE captureEvent = nullptr;

    // WASAPI has no push-style IOProc like CoreAudio - this thread is the functional equivalent
    // of the mac backend's IOProc block, pulling packets via captureClient in a loop instead.
    struct CaptureThread final : public juce::Thread
    {
        CaptureThread() : juce::Thread("SystemAudioTap capture") {}
        void run() override;
        Impl* impl = nullptr;
    };

    std::unique_ptr<CaptureThread> captureThread;

    float deinterleavedLeft[maxFramesPerCallback];
    float deinterleavedRight[maxFramesPerCallback];

    double sampleRate = 48000.0;
    int numChannels = 2;

    AudioCallback callback;
    bool running = false;

    bool comInitializedHere = false;

    juce::String lastError;
};

SystemAudioTap::SystemAudioTap() : _impl(std::make_unique<Impl>()) {}

SystemAudioTap::~SystemAudioTap()
{
    stop();
}

juce::String SystemAudioTap::getLastError() const
{
    return _impl->lastError;
}

bool SystemAudioTap::isSupported()
{
    // Process-loopback activation (AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK) requires Windows
    // 10 build 20348 (the "2020 Update") or later. Probing for the entry point rather than parsing
    // a version number mirrors the mac backend's @available(macOS 14.2, *) check - "does the OS
    // actually have this" rather than "is the OS new enough that it should."
    static const bool supported = []
    {
        HMODULE mmdevapi = LoadLibraryW(L"Mmdevapi.dll");
        if (mmdevapi == nullptr)
            return false;

        bool hasEntryPoint = GetProcAddress(mmdevapi, "ActivateAudioInterfaceAsync") != nullptr;
        FreeLibrary(mmdevapi);
        return hasEntryPoint;
    }();

    return supported;
}

bool SystemAudioTap::isRunning() const
{
    return _impl->running;
}

void SystemAudioTap::stop()
{
    if (! _impl->running)
        return;

    if (_impl->captureThread != nullptr)
    {
        _impl->captureThread->signalThreadShouldExit();
        if (_impl->captureEvent != nullptr)
            SetEvent(_impl->captureEvent); // wake the thread out of its wait so it notices the exit signal
        _impl->captureThread->waitForThreadToExit(2000);
        _impl->captureThread.reset();
    }

    if (_impl->audioClient != nullptr)
        _impl->audioClient->Stop();

    if (_impl->captureClient != nullptr)
    {
        _impl->captureClient->Release();
        _impl->captureClient = nullptr;
    }

    if (_impl->audioClient != nullptr)
    {
        _impl->audioClient->Release();
        _impl->audioClient = nullptr;
    }

    if (_impl->captureEvent != nullptr)
    {
        CloseHandle(_impl->captureEvent);
        _impl->captureEvent = nullptr;
    }

    _impl->callback = nullptr;
    _impl->running = false;

    if (_impl->comInitializedHere)
    {
        CoUninitialize();
        _impl->comInitializedHere = false;
    }
}

void SystemAudioTap::Impl::CaptureThread::run()
{
    while (! threadShouldExit())
    {
        if (WaitForSingleObject(impl->captureEvent, 200) != WAIT_OBJECT_0)
            continue;

        if (threadShouldExit())
            break;

        UINT32 packetLength = 0;
        if (FAILED(impl->captureClient->GetNextPacketSize(&packetLength)))
            continue;

        while (packetLength != 0)
        {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            if (FAILED(impl->captureClient->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr)))
                break;

            if (impl->callback && numFramesAvailable > 0 && (flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0)
            {
                int numFrames = juce::jmin((int) numFramesAvailable, maxFramesPerCallback);
                const auto* interleaved = (const float*) data;

                for (int i = 0; i < numFrames; ++i)
                {
                    impl->deinterleavedLeft[i] = interleaved[i * impl->numChannels];
                    impl->deinterleavedRight[i] = impl->numChannels > 1
                        ? interleaved[i * impl->numChannels + 1]
                        : interleaved[i * impl->numChannels];
                }

                const float* channelData[2] { impl->deinterleavedLeft, impl->deinterleavedRight };
                impl->callback(channelData, 2, numFrames, impl->sampleRate);
            }

            impl->captureClient->ReleaseBuffer(numFramesAvailable);

            if (FAILED(impl->captureClient->GetNextPacketSize(&packetLength)))
                break;
        }
    }
}

bool SystemAudioTap::start(int targetProcessID, AudioCallback callback)
{
    stop();
    _impl->lastError.clear();

    if (! isSupported())
    {
        _impl->lastError = "Unsupported OS (requires Windows 10 build 20348+ / the 2020 Update)";
        DBG("SystemAudioTap: " << _impl->lastError);
        return false;
    }

    // Neither this function nor the rest of this file initialized COM before this point despite
    // relying on it throughout (ActivateAudioInterfaceAsync below) - it "worked" only because
    // something else in the process happened to already initialize it as a side effect.
    // SUCCEEDED() covers both a fresh init (S_OK) and "already initialized with the same apartment
    // model" (S_FALSE); RPC_E_CHANGED_MODE (a different apartment already set by someone else)
    // isn't SUCCEEDED, but COM is still usable on this thread regardless - comInitializedHere just
    // tracks whether *we're* the ones responsible for the matching CoUninitialize() in stop().
    _impl->comInitializedHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    AUDIOCLIENT_ACTIVATION_PARAMS activationParams {};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    activationParams.ProcessLoopbackParams.TargetProcessId = (DWORD) targetProcessID;

    PROPVARIANT activateParams {};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(activationParams);
    activateParams.blob.pBlobData = (BYTE*) &activationParams;

    auto completionHandler = Microsoft::WRL::Make<ActivationCompletionHandler>();
    completionHandler->completedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (completionHandler->completedEvent == nullptr)
    {
        _impl->lastError = "CreateEventW failed for the activation completion event (error=" + juce::String((int) GetLastError()) + ")";
        DBG("SystemAudioTap: " << _impl->lastError);
        return false;
    }

    IActivateAudioInterfaceAsyncOperation* asyncOp = nullptr;
    HRESULT status = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient), &activateParams, completionHandler.Get(), &asyncOp);

    if (FAILED(status))
    {
        _impl->lastError = "ActivateAudioInterfaceAsync failed (hr=0x" + juce::String::toHexString((int) status) + ")";
        DBG("SystemAudioTap: " << _impl->lastError);
        CloseHandle(completionHandler->completedEvent);
        return false;
    }

    WaitForSingleObject(completionHandler->completedEvent, INFINITE);
    CloseHandle(completionHandler->completedEvent);
    if (asyncOp != nullptr)
        asyncOp->Release();

    if (FAILED(completionHandler->result) || completionHandler->audioClient == nullptr)
    {
        _impl->lastError = "Process-loopback activation failed (hr=0x" + juce::String::toHexString((int) completionHandler->result) + ")";
        DBG("SystemAudioTap: " << _impl->lastError);
        return false;
    }

    _impl->audioClient = completionHandler->audioClient;

    // GetMixFormat() is documented to fail (E_NOTIMPL) on this virtual process-loopback device, so
    // - unlike the mac backend, which reads the tap's real format - this backend always requests
    // its own fixed format and relies on AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM to have the audio
    // engine convert to it.
    WAVEFORMATEX captureFormat {};
    captureFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    captureFormat.nChannels = 2;
    captureFormat.nSamplesPerSec = 48000;
    captureFormat.wBitsPerSample = 32;
    captureFormat.nBlockAlign = (WORD) (captureFormat.nChannels * captureFormat.wBitsPerSample / 8);
    captureFormat.nAvgBytesPerSec = captureFormat.nSamplesPerSec * captureFormat.nBlockAlign;

    status = _impl->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        0, 0, &captureFormat, nullptr);

    if (FAILED(status))
    {
        _impl->lastError = "IAudioClient::Initialize failed (hr=0x" + juce::String::toHexString((int) status) + ")";
        DBG("SystemAudioTap: " << _impl->lastError);
        _impl->audioClient->Release();
        _impl->audioClient = nullptr;
        return false;
    }

    _impl->captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (_impl->captureEvent == nullptr || FAILED(_impl->audioClient->SetEventHandle(_impl->captureEvent)))
    {
        _impl->lastError = "SetEventHandle failed";
        DBG("SystemAudioTap: " << _impl->lastError);
        _impl->audioClient->Release();
        _impl->audioClient = nullptr;
        return false;
    }

    status = _impl->audioClient->GetService(IID_PPV_ARGS(&_impl->captureClient));
    if (FAILED(status))
    {
        _impl->lastError = "GetService(IAudioCaptureClient) failed (hr=0x" + juce::String::toHexString((int) status) + ")";
        DBG("SystemAudioTap: " << _impl->lastError);
        CloseHandle(_impl->captureEvent);
        _impl->captureEvent = nullptr;
        _impl->audioClient->Release();
        _impl->audioClient = nullptr;
        return false;
    }

    _impl->sampleRate = captureFormat.nSamplesPerSec;
    _impl->numChannels = captureFormat.nChannels;
    _impl->callback = std::move(callback);

    status = _impl->audioClient->Start();
    if (FAILED(status))
    {
        _impl->lastError = "IAudioClient::Start failed (hr=0x" + juce::String::toHexString((int) status) + ")";
        DBG("SystemAudioTap: " << _impl->lastError);
        _impl->captureClient->Release();
        _impl->captureClient = nullptr;
        CloseHandle(_impl->captureEvent);
        _impl->captureEvent = nullptr;
        _impl->audioClient->Release();
        _impl->audioClient = nullptr;
        _impl->callback = nullptr;
        return false;
    }

    _impl->captureThread = std::make_unique<Impl::CaptureThread>();
    _impl->captureThread->impl = _impl.get();
    _impl->captureThread->startThread(juce::Thread::Priority::high);

    // Unlike the mac backend (tapDescription.muteBehavior = CATapMuted), this is a pure copy tap -
    // the browser's own audio still plays through the system's default output at the same time it's
    // captured here, so Standalone (and the direct-output side of the VST3 bug) hears it twice.
    // A mute-on-capture feature was attempted and reverted: see
    // docs/WINDOWS_AUDIO_CAPTURE_MUTE_LIMITATION.md for why (WASAPI process-loopback reports
    // AUDCLNT_BUFFERFLAGS_SILENT - meaning our own capture gets nothing - for as long as the
    // target session's audible output is zero, via mute or volume, with no viable workaround
    // available on a single-output-device machine).

    _impl->running = true;
    return true;
}

}
