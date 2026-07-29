#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

#include <algorithm>
#include <string>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

TEST_CASE("ProcessList::isSupported matches the declared per-platform support matrix", "[ProcessList]")
{
#if JUCE_MAC || JUCE_WINDOWS
    CHECK(audiocapture::ProcessList::isSupported());
#else
    CHECK_FALSE(audiocapture::ProcessList::isSupported());
#endif
}

TEST_CASE("ProcessList enumeration and filtering on unsupported platforms is uniformly empty", "[ProcessList]")
{
    if (audiocapture::ProcessList::isSupported())
        return; // covered by the "supported platforms" test below instead

    CHECK(audiocapture::ProcessList::getAllProcesses().empty());
    // These both route through the real (non-mocked) filtering code internally, just over a
    // guaranteed-empty getAllProcesses() result on this platform.
    CHECK(audiocapture::ProcessList::filterProcesses([](const audiocapture::ProcessInfo&) { return true; }).empty());
    CHECK(audiocapture::ProcessList::getMainApplicationProcesses().empty());
}

TEST_CASE("ProcessList enumerates real OS processes and filters them correctly when supported", "[ProcessList]")
{
    if (! audiocapture::ProcessList::isSupported())
        return; // covered by the "unsupported platforms" test above instead

    const auto baseline = audiocapture::ProcessList::getAllProcesses();
    CHECK(! baseline.empty());

    // Passthrough / exclusion properties of filterProcesses(), checked against a live, real,
    // necessarily-nondeterministic process list - so only structural properties are asserted, not
    // exact contents.
    const auto allMatched = audiocapture::ProcessList::filterProcesses([](const audiocapture::ProcessInfo&) { return true; });
    CHECK(allMatched.size() == baseline.size());

    const auto noneMatched = audiocapture::ProcessList::filterProcesses([](const audiocapture::ProcessInfo&) { return false; });
    CHECK(noneMatched.empty());

    const auto mainApps = audiocapture::ProcessList::getMainApplicationProcesses();
    CHECK(mainApps.size() <= baseline.size());
    for (auto& process : mainApps)
        CHECK(process.isMainApplication);

    // Proves the enumeration is real, not stubbed: this very test process should be discoverable.
#if JUCE_WINDOWS
    const auto currentProcessID = (int) GetCurrentProcessId();
#else
    const auto currentProcessID = (int) getpid();
#endif
    const auto selfFound = std::any_of(baseline.begin(), baseline.end(),
        [currentProcessID](const audiocapture::ProcessInfo& process) { return process.processID == currentProcessID; });
    CHECK(selfFound);
}

TEST_CASE("ProcessList::getDescendantProcessIDs always includes the root, even with no real descendants", "[ProcessList]")
{
    if (! audiocapture::ProcessList::isSupported())
        return;

#if JUCE_WINDOWS
    const auto currentProcessID = (int) GetCurrentProcessId();
#else
    const auto currentProcessID = (int) getpid();
#endif

    const auto descendants = audiocapture::ProcessList::getDescendantProcessIDs(currentProcessID);
    CHECK(descendants.count(currentProcessID) == 1);

    // A PID that (almost certainly) doesn't correspond to any running process still yields itself
    // as the sole member - the root is always included regardless of whether it's a real, findable
    // process (parentProcessID is only actually populated on Windows today; on other platforms this
    // is also the only meaningful assertion available).
    constexpr int implausiblePID = 999999999;
    const auto onlyRoot = audiocapture::ProcessList::getDescendantProcessIDs(implausiblePID);
    CHECK(onlyRoot.size() == 1);
    CHECK(onlyRoot.count(implausiblePID) == 1);
}

#if JUCE_WINDOWS
TEST_CASE("ProcessList::getDescendantProcessIDs discovers a real child process on Windows", "[ProcessList]")
{
    const auto currentProcessID = (int) GetCurrentProcessId();

    // A short-lived child process that stays alive long enough for the check below.
    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo {};

    std::wstring commandLine = L"cmd.exe /c ping -n 6 127.0.0.1 >nul";
    const bool spawned = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo) != 0;
    REQUIRE(spawned);

    const auto childProcessID = (int) processInfo.dwProcessId;

    const auto descendants = audiocapture::ProcessList::getDescendantProcessIDs(currentProcessID);
    CHECK(descendants.count(currentProcessID) == 1);
    CHECK(descendants.count(childProcessID) == 1);

    TerminateProcess(processInfo.hProcess, 0);
    WaitForSingleObject(processInfo.hProcess, 2000);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
}
#endif
