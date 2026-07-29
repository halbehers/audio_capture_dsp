#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace audiocapture
{

struct ProcessInfo
{
    int processID = 0;
    int parentProcessID = 0;      // 0 when unavailable/unsupported on this platform
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

    // rootProcessID plus every process transitively parented under it (children, grandchildren,
    // etc.), per parentProcessID links from the same snapshot getAllProcesses() already takes.
    // Needed anywhere a whole process tree must be identified one-by-one rather than relied on
    // implicitly (e.g. muting every WASAPI audio session belonging to a browser's renderer/GPU/
    // audio-service subprocesses on Windows - see SystemAudioTap_windows.cpp) - contrast with
    // process-tree-aware OS APIs like Windows' PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE,
    // which don't need this because they already operate on the whole tree internally.
    static std::unordered_set<int> getDescendantProcessIDs(int rootProcessID)
    {
        const auto allProcesses = getAllProcesses();

        std::unordered_set<int> result { rootProcessID };
        bool addedAny = true;

        // Fixed-point loop rather than a parent->children map: getAllProcesses() is a snapshot
        // few tens/hundreds of entries at most, so the O(n) rescans this costs are negligible
        // against the simplicity of not building an index only used once per call.
        while (addedAny)
        {
            addedAny = false;

            for (const auto& process : allProcesses)
            {
                if (result.count(process.processID) != 0)
                    continue;

                if (result.count(process.parentProcessID) != 0)
                {
                    result.insert(process.processID);
                    addedAny = true;
                }
            }
        }

        return result;
    }
};

}
