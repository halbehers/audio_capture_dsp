#include "../../../include/capture/SystemAudioTap.h"

// Catch-all for any platform other than macOS, Windows, and Linux (e.g. BSD): preserves this
// module's graceful-degradation guarantee that it compiles and links everywhere, reporting
// unsupported rather than failing to build.
namespace audiocapture
{

struct SystemAudioTap::Impl
{
    AudioCallback callback;
    bool running = false;
};

SystemAudioTap::SystemAudioTap() : _impl(std::make_unique<Impl>()) {}

SystemAudioTap::~SystemAudioTap() = default;

bool SystemAudioTap::isSupported()
{
    return false;
}

bool SystemAudioTap::isRunning() const
{
    return _impl->running;
}

juce::String SystemAudioTap::getLastError() const
{
    return "Unsupported platform";
}

void SystemAudioTap::stop()
{
}

bool SystemAudioTap::start(int targetProcessID, AudioCallback callback)
{
    juce::ignoreUnused(targetProcessID, callback);
    return false;
}

}
