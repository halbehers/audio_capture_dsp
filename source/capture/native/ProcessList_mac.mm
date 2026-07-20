#include "../../../include/capture/ProcessList.h"

#import <AppKit/AppKit.h>
#include <libproc.h>

namespace audiocapture
{

bool ProcessList::isSupported()
{
    return true;
}

std::vector<ProcessInfo> ProcessList::getAllProcesses()
{
    std::vector<ProcessInfo> result;

    // NSWorkspace only knows about registered *applications*, but gives the best signal for both
    // a nice display name and "is this a real user-facing app" (activationPolicy). proc_listpids
    // gives every process the OS will report, application or not - combine both: use the
    // NSWorkspace-derived name/path/isMainApplication when a PID matches, else fall back to raw
    // libproc data.
    NSMutableDictionary<NSNumber*, NSRunningApplication*>* appsByPID = [NSMutableDictionary new];
    for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
        appsByPID[@(app.processIdentifier)] = app;

    int numBytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (numBytes <= 0)
        return result;

    std::vector<pid_t> pids ((size_t) numBytes / sizeof(pid_t) + 32); // some slack - process count can grow between calls
    int actualBytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int) (pids.size() * sizeof(pid_t)));
    if (actualBytes <= 0)
        return result;

    int numPids = actualBytes / (int) sizeof(pid_t);
    result.reserve((size_t) numPids);

    for (int i = 0; i < numPids; ++i)
    {
        pid_t pid = pids[(size_t) i];
        if (pid <= 0)
            continue;

        ProcessInfo info;
        info.processID = (int) pid;

        NSRunningApplication* app = appsByPID[@(pid)];
        if (app != nil)
        {
            const char* localizedNameUTF8 = app.localizedName.UTF8String;
            info.name = localizedNameUTF8 != nullptr ? std::string(localizedNameUTF8) : std::string();

            const char* bundlePathUTF8 = app.bundleURL.path.UTF8String;
            info.executablePath = bundlePathUTF8 != nullptr ? std::string(bundlePathUTF8) : std::string();

            info.isMainApplication = (app.activationPolicy == NSApplicationActivationPolicyRegular);
        }
        else
        {
            char pathBuffer[PROC_PIDPATHINFO_MAXSIZE] {};
            int pathLen = proc_pidpath(pid, pathBuffer, sizeof(pathBuffer));
            info.executablePath = pathLen > 0 ? std::string(pathBuffer, (size_t) pathLen) : std::string();

            char nameBuffer[256] {};
            int nameLen = proc_name(pid, nameBuffer, sizeof(nameBuffer));
            info.name = nameLen > 0 ? std::string(nameBuffer, (size_t) nameLen) : std::string();
        }

        result.push_back(std::move(info));
    }

    return result;
}

}
