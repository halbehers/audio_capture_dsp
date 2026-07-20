#pragma once

#include <functional>
#include <string>
#include <vector>

namespace audiocapture
{

struct ProcessInfo
{
    int processID = 0;
    std::string name;             // display name, e.g. "Firefox" - falls back to the raw
                                   // executable name when no nicer name is available
    std::string executablePath;   // full path when available, empty otherwise
    bool isMainApplication = false; // looks like a user-facing app, not a background/helper process
};

// Enumerates OS processes for resolving a target PID (e.g. for SystemAudioTap::start() or a
// ProcessAudioCapture::getProcessID() override) - not tied to any particular target process or
// product.
class ProcessList
{
public:
    // Whether this platform can enumerate processes at all.
    static bool isSupported();

    // Snapshots every currently-running process the OS will report.
    static std::vector<ProcessInfo> getAllProcesses();

    // General-purpose filter over getAllProcesses().
    static std::vector<ProcessInfo> filterProcesses(const std::function<bool(const ProcessInfo&)>& predicate)
    {
        std::vector<ProcessInfo> result;
        for (auto& process : getAllProcesses())
            if (predicate(process))
                result.push_back(process);
        return result;
    }

    // Convenience for the common case this class exists for: only processes that look like
    // user-facing "main" applications (e.g. Firefox, QuickTime, VLC), not background/helper
    // processes.
    static std::vector<ProcessInfo> getMainApplicationProcesses()
    {
        return filterProcesses([](const ProcessInfo& process) { return process.isMainApplication; });
    }
};

}
