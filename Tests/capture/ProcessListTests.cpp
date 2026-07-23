#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

#include <algorithm>

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
