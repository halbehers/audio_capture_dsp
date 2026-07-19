#include "../../../include/capture/SystemAudioTap.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <cstdlib>
#include <cstring>

namespace audiocapture
{

struct SystemAudioTap::Impl
{
    static constexpr int maxFramesPerCallback = 1 << 14;

    // Deliberately not integrated with JUCE's message thread - pw_thread_loop is PipeWire's own
    // dedicated-thread abstraction, and the stream's process callback (invoked on it) is the
    // functional equivalent of the mac backend's IOProc block / the Windows backend's capture
    // thread loop.
    pw_thread_loop* threadLoop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    pw_stream* stream = nullptr;

    spa_hook registryListener {};
    spa_hook coreListener {};
    spa_hook streamListener {};

    // PID-resolution state, only meaningful during start().
    int targetPID = 0;
    uint32_t resolvedNodeID = PW_ID_ANY;
    bool resolved = false;
    int pendingSyncSeq = -1;
    bool syncDone = false;

    float deinterleavedLeft[maxFramesPerCallback];
    float deinterleavedRight[maxFramesPerCallback];

    double sampleRate = 48000.0;
    int numChannels = 2;

    AudioCallback callback;
    bool running = false;
};

namespace
{
    constexpr int syncTimeoutSeconds = 3;

    void onRegistryGlobal(void* data, uint32_t id, uint32_t /*permissions*/, const char* type,
        uint32_t /*version*/, const spa_dict* props)
    {
        auto* impl = static_cast<SystemAudioTap::Impl*>(data);

        if (impl->resolved || props == nullptr || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
            return;

        const char* pidStr = spa_dict_lookup(props, PW_KEY_APP_PROCESS_ID);
        if (pidStr == nullptr || std::atoi(pidStr) != impl->targetPID)
            return;

        impl->resolvedNodeID = id;
        impl->resolved = true;
        pw_thread_loop_signal(impl->threadLoop, false);
    }

    void onCoreDone(void* data, uint32_t id, int seq)
    {
        auto* impl = static_cast<SystemAudioTap::Impl*>(data);
        if (id == PW_ID_CORE && seq == impl->pendingSyncSeq)
        {
            impl->syncDone = true;
            pw_thread_loop_signal(impl->threadLoop, false);
        }
    }

    void onStreamParamChanged(void* data, uint32_t id, const spa_pod* param)
    {
        auto* impl = static_cast<SystemAudioTap::Impl*>(data);
        if (param == nullptr || id != SPA_PARAM_Format)
            return;

        spa_audio_info_raw info {};
        if (spa_format_audio_raw_parse(param, &info) >= 0)
        {
            impl->sampleRate = info.rate;
            impl->numChannels = (int) info.channels;
        }
    }

    void onStreamProcess(void* data)
    {
        auto* impl = static_cast<SystemAudioTap::Impl*>(data);

        pw_buffer* b = pw_stream_dequeue_buffer(impl->stream);
        if (b == nullptr)
            return;

        spa_buffer* buf = b->buffer;
        if (buf->datas[0].data != nullptr && buf->datas[0].chunk != nullptr && impl->callback)
        {
            int stride = (int) sizeof(float) * impl->numChannels;
            int numFrames = stride > 0 ? (int) (buf->datas[0].chunk->size / (uint32_t) stride) : 0;
            numFrames = juce::jmin(numFrames, SystemAudioTap::Impl::maxFramesPerCallback);

            const auto* interleaved = (const float*) buf->datas[0].data;

            for (int i = 0; i < numFrames; ++i)
            {
                impl->deinterleavedLeft[i] = interleaved[i * impl->numChannels];
                impl->deinterleavedRight[i] = impl->numChannels > 1
                    ? interleaved[i * impl->numChannels + 1]
                    : interleaved[i * impl->numChannels];
            }

            if (numFrames > 0)
            {
                const float* channelData[2] { impl->deinterleavedLeft, impl->deinterleavedRight };
                impl->callback(channelData, 2, numFrames, impl->sampleRate);
            }
        }

        pw_stream_queue_buffer(impl->stream, b);
    }
}

SystemAudioTap::SystemAudioTap() : _impl(std::make_unique<Impl>()) {}

SystemAudioTap::~SystemAudioTap()
{
    stop();
}

bool SystemAudioTap::isSupported()
{
    // Unlike the mac/Windows checks (both cheap, static "is the OS new enough" queries), "supported"
    // on Linux really means "a PipeWire daemon is reachable right now" - there's no OS-version
    // signal to key off, since PipeWire is a userspace daemon that may or may not be running
    // regardless of distro/kernel version. This does a bounded, one-off connect-and-disconnect
    // probe rather than a static check.
    static const bool supported = []
    {
        pw_init(nullptr, nullptr);

        pw_thread_loop* probeLoop = pw_thread_loop_new("SystemAudioTap-probe", nullptr);
        if (probeLoop == nullptr)
            return false;

        bool reachable = false;
        if (pw_thread_loop_start(probeLoop) == 0)
        {
            pw_thread_loop_lock(probeLoop);
            pw_context* probeContext = pw_context_new(pw_thread_loop_get_loop(probeLoop), nullptr, 0);
            pw_core* probeCore = probeContext != nullptr ? pw_context_connect(probeContext, nullptr, 0) : nullptr;
            reachable = probeCore != nullptr;

            if (probeCore != nullptr)
                pw_core_disconnect(probeCore);
            if (probeContext != nullptr)
                pw_context_destroy(probeContext);
            pw_thread_loop_unlock(probeLoop);
            pw_thread_loop_stop(probeLoop);
        }

        pw_thread_loop_destroy(probeLoop);
        return reachable;
    }();

    return supported;
}

bool SystemAudioTap::isRunning() const
{
    return _impl->running;
}

void SystemAudioTap::stop()
{
    // Deliberately not gated on _impl->running: start() calls this to unwind partial state on
    // every failure path below, before running is ever set to true.
    if (_impl->threadLoop != nullptr)
    {
        pw_thread_loop_lock(_impl->threadLoop);

        if (_impl->stream != nullptr)
        {
            pw_stream_destroy(_impl->stream);
            _impl->stream = nullptr;
        }
        if (_impl->registry != nullptr)
        {
            pw_proxy_destroy((pw_proxy*) _impl->registry);
            _impl->registry = nullptr;
        }
        if (_impl->core != nullptr)
        {
            pw_core_disconnect(_impl->core);
            _impl->core = nullptr;
        }
        if (_impl->context != nullptr)
        {
            pw_context_destroy(_impl->context);
            _impl->context = nullptr;
        }

        pw_thread_loop_unlock(_impl->threadLoop);
        pw_thread_loop_stop(_impl->threadLoop);
        pw_thread_loop_destroy(_impl->threadLoop);
        _impl->threadLoop = nullptr;
    }

    _impl->callback = nullptr;
    _impl->running = false;
}

bool SystemAudioTap::start(int targetProcessID, AudioCallback callback)
{
    stop();

    if (! isSupported())
    {
        DBG("SystemAudioTap: unsupported (no reachable PipeWire daemon)");
        return false;
    }

    _impl->targetPID = targetProcessID;
    _impl->resolvedNodeID = PW_ID_ANY;
    _impl->resolved = false;
    _impl->syncDone = false;

    _impl->threadLoop = pw_thread_loop_new("SystemAudioTap", nullptr);
    if (_impl->threadLoop == nullptr || pw_thread_loop_start(_impl->threadLoop) != 0)
    {
        DBG("SystemAudioTap: failed to start PipeWire thread loop");
        return false;
    }

    pw_thread_loop_lock(_impl->threadLoop);

    _impl->context = pw_context_new(pw_thread_loop_get_loop(_impl->threadLoop), nullptr, 0);
    _impl->core = _impl->context != nullptr ? pw_context_connect(_impl->context, nullptr, 0) : nullptr;

    if (_impl->core == nullptr)
    {
        DBG("SystemAudioTap: pw_context_connect failed");
        pw_thread_loop_unlock(_impl->threadLoop);
        stop();
        return false;
    }

    pw_core_events coreEvents {};
    coreEvents.version = PW_VERSION_CORE_EVENTS;
    coreEvents.done = onCoreDone;
    pw_core_add_listener(_impl->core, &_impl->coreListener, &coreEvents, _impl.get());

    _impl->registry = pw_core_get_registry(_impl->core, PW_VERSION_REGISTRY, 0);
    if (_impl->registry == nullptr)
    {
        DBG("SystemAudioTap: pw_core_get_registry failed");
        pw_thread_loop_unlock(_impl->threadLoop);
        stop();
        return false;
    }

    pw_registry_events registryEvents {};
    registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
    registryEvents.global = onRegistryGlobal;
    pw_registry_add_listener(_impl->registry, &_impl->registryListener, &registryEvents, _impl.get());

    // Resolve targetProcessID -> a PipeWire node id: walk the registry (onRegistryGlobal fires
    // per-object as the server sends them) until either the target is found or a round-trip sync
    // confirms the whole current registry snapshot has been delivered with no match.
    _impl->pendingSyncSeq = pw_core_sync(_impl->core, PW_ID_CORE, 0);
    for (int elapsed = 0; elapsed < syncTimeoutSeconds && ! _impl->resolved && ! _impl->syncDone; ++elapsed)
        pw_thread_loop_timed_wait(_impl->threadLoop, 1);

    if (! _impl->resolved)
    {
        DBG("SystemAudioTap: no PipeWire node found for pid " << targetProcessID);
        pw_thread_loop_unlock(_impl->threadLoop);
        stop();
        return false;
    }

    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Music",
        nullptr);

    _impl->stream = pw_stream_new(_impl->core, "AudioCaptureDSP-Capture", props);
    if (_impl->stream == nullptr)
    {
        DBG("SystemAudioTap: pw_stream_new failed");
        pw_thread_loop_unlock(_impl->threadLoop);
        stop();
        return false;
    }

    pw_stream_events streamEvents {};
    streamEvents.version = PW_VERSION_STREAM_EVENTS;
    streamEvents.param_changed = onStreamParamChanged;
    streamEvents.process = onStreamProcess;
    pw_stream_add_listener(_impl->stream, &_impl->streamListener, &streamEvents, _impl.get());

    uint8_t formatBuffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(formatBuffer, sizeof(formatBuffer));
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
        &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .channels = 2, .rate = 48000));

    int connectResult = pw_stream_connect(_impl->stream, PW_DIRECTION_INPUT, _impl->resolvedNodeID,
        (pw_stream_flags) (PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);

    if (connectResult != 0)
    {
        DBG("SystemAudioTap: pw_stream_connect failed (" << connectResult << ")");
        pw_thread_loop_unlock(_impl->threadLoop);
        stop();
        return false;
    }

    pw_thread_loop_unlock(_impl->threadLoop);

    _impl->callback = std::move(callback);
    _impl->running = true;
    return true;
}

}
