#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

#include <atomic>

namespace
{
    // The only seam ProcessAudioCapture exposes to a subclass - always fails to resolve a target
    // process, so _tap.start() is never actually reached and the retry/give-up state machine can
    // be exercised with zero real CoreAudio/WASAPI/PipeWire calls.
    struct CountingProcessAudioCapture : audiocapture::ProcessAudioCapture
    {
        using audiocapture::ProcessAudioCapture::ProcessAudioCapture;
        std::atomic<int> calls { 0 };

    protected:
        bool getProcessID(int&) override
        {
            ++calls;
            return false;
        }
    };

    void pumpMessagesFor(int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
    }
}

TEST_CASE("ProcessAudioCapture retry loop exhausts on supported platforms, is a true no-op otherwise", "[ProcessAudioCapture]")
{
    acdsp::AudioCapture destination;
    CountingProcessAudioCapture capture(destination);
    capture.startCapture();

    if (! audiocapture::SystemAudioTap::isSupported())
    {
        // startCapture() early-returns before even starting the timer - getProcessID() must
        // never be called.
        pumpMessagesFor(500);
        CHECK(capture.calls.load() == 0);
        return;
    }

    // Real mac 14.2+/Windows/Linux-with-PipeWire: the timer polls every 250ms. Loose bound to
    // tolerate CI scheduler jitter.
    pumpMessagesFor(600);
    const int callsAfterFirstTicks = capture.calls.load();
    CHECK(callsAfterFirstTicks >= 1);

    // maxRetries = 60 at retryIntervalMs = 250 => ~15s nominal, but shared/throttled CI runners
    // can make juce::Timer tick noticeably slower than its nominal interval (observed as low as
    // 50/60 retries within the nominal ~15.5s window on a GitHub Actions macOS runner) - poll in
    // small increments up to a generous hard timeout instead of assuming a fixed wall-clock
    // duration is enough to reach exhaustion.
    constexpr int pollIncrementMs = 250;
    constexpr int hardTimeoutMs = 60000; // generous margin over the ~15s nominal exhaustion time
    int elapsedMs = 0;
    while (capture.calls.load() < 60 && elapsedMs < hardTimeoutMs)
    {
        pumpMessagesFor(pollIncrementMs);
        elapsedMs += pollIncrementMs;
    }

    const int callsAtExhaustion = capture.calls.load();
    REQUIRE(callsAtExhaustion == 60); // must genuinely reach exhaustion within the hard timeout, not just give up polling

    // Must not keep retrying past exhaustion.
    pumpMessagesFor(500);
    CHECK(capture.calls.load() == callsAtExhaustion);
}

TEST_CASE("ProcessAudioCapture destructor stops capture safely mid-run", "[ProcessAudioCapture]")
{
    acdsp::AudioCapture destination;
    {
        CountingProcessAudioCapture capture(destination);
        capture.startCapture();
        // capture goes out of scope immediately - destructor calls stopCapture() (stopTimer() +
        // tap.stop()) while a retry cycle may still be pending.
    }
    SUCCEED("destructor did not crash");
}

TEST_CASE("ProcessAudioCapture stop/start cycle twice in a row is safe", "[ProcessAudioCapture]")
{
    acdsp::AudioCapture destination;
    CountingProcessAudioCapture capture(destination);

    REQUIRE_NOTHROW(capture.startCapture());
    REQUIRE_NOTHROW(capture.stopCapture());
    REQUIRE_NOTHROW(capture.startCapture());
    REQUIRE_NOTHROW(capture.stopCapture());

    if (! audiocapture::SystemAudioTap::isSupported())
        CHECK(capture.calls.load() == 0);
}
