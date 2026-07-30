#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

#include <atomic>
#include <cmath>
#include <thread>

TEST_CASE("LatencyMonitor::consumeLatencyMs returns 0 before anything has been produced", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;
    CHECK(monitor.consumeLatencyMs(0) == Catch::Approx(0.0).margin(0.0));
}

TEST_CASE("LatencyMonitor::consumeLatencyMs reflects real dwell time for produced-but-unconsumed units", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;
    monitor.recordProduced(256);

    juce::Thread::sleep(50);

    const double latencyMs = monitor.consumeLatencyMs(256);
    CHECK(latencyMs >= 30.0); // margin under the 50ms sleep to tolerate scheduler slop
}

TEST_CASE("LatencyMonitor::consumeLatencyMs settles at 0 once fully caught up and idle", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;
    monitor.recordProduced(256);
    monitor.consumeLatencyMs(256); // fully catches up

    juce::Thread::sleep(50);

    // Regression guard: idling once caught up (nothing new produced or consumed) must not keep
    // dwelling against an increasingly stale production timestamp.
    CHECK(monitor.consumeLatencyMs(0) == Catch::Approx(0.0).margin(0.0));
}

TEST_CASE("LatencyMonitor::reset() clears prior history so measurement starts fresh", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;
    monitor.recordProduced(256);

    juce::Thread::sleep(50);

    // Without a reset, this stale production record would report ~50ms of dwell time.
    monitor.reset();
    CHECK(monitor.consumeLatencyMs(0) == Catch::Approx(0.0).margin(0.0));

    // Confirm the monitor is genuinely usable again afterwards, not just permanently zeroed.
    monitor.recordProduced(128);
    CHECK(monitor.consumeLatencyMs(128) < 5.0); // consumed essentially immediately after producing
}

TEST_CASE("LatencyMonitor::recordProduced(<=0) is a no-op", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;
    monitor.recordProduced(0);
    monitor.recordProduced(-5);

    // Neither call should have armed the "something was produced" sentinel.
    CHECK(monitor.consumeLatencyMs(0) == Catch::Approx(0.0).margin(0.0));
}

TEST_CASE("LatencyMonitor::consumeLatencyMs stays finite and non-negative across ring wraparound", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;

    // More iterations than the ring's fixed capacity (256), with no intervening consumes, to
    // exercise the ring overwriting its own oldest entries.
    for (int i = 0; i < 300; ++i)
        monitor.recordProduced(1);

    const double latencyMs = monitor.consumeLatencyMs(0);
    CHECK(std::isfinite(latencyMs));
    CHECK(latencyMs >= 0.0);

    // Second variant: move the read cursor off 0 first via a real produce/consume pair, then
    // push it far more than ringCapacity writes behind before consuming again - stresses the
    // "stale cursor now more than 256 writes behind" case. The exact returned value isn't pinned
    // (the class doesn't defend against a caller violating the "consume roughly as fast as you
    // produce" contract) - only finiteness and non-negativity are guaranteed.
    monitor.recordProduced(1);
    CHECK(monitor.consumeLatencyMs(1) >= 0.0);

    for (int i = 0; i < 300; ++i)
        monitor.recordProduced(1);

    const double afterSecondWrap = monitor.consumeLatencyMs(0);
    CHECK(std::isfinite(afterSecondWrap));
    CHECK(afterSecondWrap >= 0.0);
}

TEST_CASE("LatencyMonitor survives a real single-producer/single-consumer thread race", "[LatencyMonitor]")
{
    acdsp::LatencyMonitor monitor;
    std::atomic<bool> stop { false };
    std::atomic<bool> sawNegativeOrNonFinite { false };

    std::thread producer([&]
    {
        while (! stop.load(std::memory_order_relaxed))
        {
            monitor.recordProduced(1);
            std::this_thread::yield();
        }
    });

    std::thread consumer([&]
    {
        while (! stop.load(std::memory_order_relaxed))
        {
            const double ms = monitor.consumeLatencyMs(1);
            if (! std::isfinite(ms) || ms < 0.0)
                sawNegativeOrNonFinite.store(true, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    juce::Thread::sleep(200);
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();

    // Only post-join invariants are asserted here - real value is running this test under
    // ThreadSanitizer to catch a missing acquire/release or genuine data race.
    CHECK_FALSE(sawNegativeOrNonFinite.load());
}
