#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

#include <atomic>

TEST_CASE("SystemAudioTap default-constructed is not running", "[SystemAudioTap]")
{
    audiocapture::SystemAudioTap tap;
    CHECK_FALSE(tap.isRunning());
}

TEST_CASE("SystemAudioTap::stop() on a never-started tap is a safe no-op", "[SystemAudioTap]")
{
    audiocapture::SystemAudioTap tap;
    REQUIRE_NOTHROW(tap.stop());
    CHECK_FALSE(tap.isRunning());
}

TEST_CASE("SystemAudioTap::stop() is idempotent", "[SystemAudioTap]")
{
    audiocapture::SystemAudioTap tap;
    REQUIRE_NOTHROW(tap.stop());
    REQUIRE_NOTHROW(tap.stop());
    CHECK_FALSE(tap.isRunning());
}

TEST_CASE("SystemAudioTap::start() is a true no-op when unsupported", "[SystemAudioTap]")
{
    if (audiocapture::SystemAudioTap::isSupported())
    {
        // Real mac 14.2+/Windows/Linux-with-PipeWire hardware - starting a tap against a real
        // other process's live audio isn't automatable here (see Tests/README.md for the manual
        // verification procedure), so this test only has something deterministic to check on
        // platforms/OS versions where support is genuinely absent.
        SUCCEED("SystemAudioTap is supported on this platform/OS version - nothing to verify here");
        return;
    }

    audiocapture::SystemAudioTap tap;
    std::atomic<bool> callbackInvoked { false };

    const bool started = tap.start(-1, [&](const float* const*, int, int, double)
    {
        callbackInvoked.store(true, std::memory_order_relaxed);
    });

    CHECK_FALSE(started);
    CHECK_FALSE(tap.isRunning());

    juce::Thread::sleep(50); // give any (incorrect) async path a chance to fire
    CHECK_FALSE(callbackInvoked.load());

    tap.stop();
    CHECK_FALSE(tap.isRunning());
}
