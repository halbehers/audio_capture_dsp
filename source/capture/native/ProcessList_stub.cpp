#include "../../../include/capture/ProcessList.h"

// Catch-all for any platform without a ProcessList backend yet (currently: Linux, and anything
// other than macOS/Windows) - preserves this module's graceful-degradation guarantee that it
// compiles and links everywhere, reporting unsupported rather than failing to build.
namespace audiocapture
{

bool ProcessList::isSupported()
{
    return false;
}

std::vector<ProcessInfo> ProcessList::getAllProcesses()
{
    return {};
}

}
