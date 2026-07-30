#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <audio_capture_dsp/audio_capture_dsp.h>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace
{
    struct ReadResult
    {
        std::vector<float> left, right;
    };

    // Mirrors a plugin's own read pattern (via AudioCapture::process), but drains everything
    // ready rather than a fixed host buffer size.
    ReadResult readAll(acdsp::AudioCapture& capture)
    {
        juce::AudioBuffer<float> buffer(2, 1 << 16); // oversized so one process() call drains everything buffered
        int numRead = capture.process(buffer);

        ReadResult result;
        result.left.resize((size_t) numRead);
        result.right.resize((size_t) numRead);
        for (int i = 0; i < numRead; ++i)
        {
            result.left[(size_t) i] = buffer.getSample(0, i);
            result.right[(size_t) i] = buffer.getSample(1, i);
        }
        return result;
    }
}

TEST_CASE("AudioCapture passes 1:1 sample rate through with negligible error", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    // A constant signal per channel, not a sine: the interpolator has an inherent group delay
    // (a few samples, even at a 1:1 ratio - see GenericInterpolator::getBaseLatency()), which
    // shows up as phase-shift error against a moving reference signal. That's real, correct
    // interpolator behaviour, not a bug worth testing for here - a constant value is immune to
    // it, so this isolates "does 1:1 passthrough preserve the value" without conflating it with
    // the interpolator's unrelated (and expected) latency characteristics.
    constexpr int numSamples = 256;
    std::vector<float> left(numSamples, 0.5f), right(numSamples, -0.3f);

    const float* channelData[2] { left.data(), right.data() };
    capture.pushAudioBlock(channelData, 2, numSamples, 48000.0);

    auto result = readAll(capture);
    REQUIRE(result.left.size() == (size_t) numSamples);

    // Skip the first few samples: the interpolator's internal history needs to "warm up" (starts
    // zero-filled) before its output is meaningful - a normal characteristic of any FIR-style
    // interpolator, not a bug.
    for (size_t i = 8; i < result.left.size(); ++i)
    {
        INFO("sample index " << i);
        CHECK(result.left[i] == Catch::Approx(0.5f).margin(0.001));
        CHECK(result.right[i] == Catch::Approx(-0.3f).margin(0.001));
    }

    CHECK(capture.totalBlocksReceived.load() == 1);
}

TEST_CASE("AudioCapture resamples between different source and target rates", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(44100.0);

    constexpr int numSamples = 4800; // 0.1s at 48kHz
    constexpr float frequency = 1000.0f;
    std::vector<float> left((size_t) numSamples), right((size_t) numSamples);
    for (size_t i = 0; i < (size_t) numSamples; ++i)
    {
        left[i] = std::sin(2.0f * juce::MathConstants<float>::pi * frequency * (float) i / 48000.0f);
        right[i] = left[i];
    }

    const float* channelData[2] { left.data(), right.data() };
    capture.pushAudioBlock(channelData, 2, numSamples, 48000.0);

    auto result = readAll(capture);
    REQUIRE(result.left.size() > 0);

    // 48000 -> 44100 should yield roughly numSamples * (44100/48000) output samples.
    const double expectedOutputSamples = numSamples * (44100.0 / 48000.0);
    CHECK((double) result.left.size() == Catch::Approx(expectedOutputSamples).margin(4.0));

    // Sanity check the resampled signal still oscillates (i.e. isn't silence/garbage) by counting
    // zero crossings - a wide tolerance since this is a sanity check, not a precision test.
    int zeroCrossings = 0;
    for (size_t i = 1; i < result.left.size(); ++i)
        if ((result.left[i - 1] < 0.0f) != (result.left[i] < 0.0f))
            ++zeroCrossings;

    // ~1kHz over ~0.1s => ~200 zero crossings (2 per cycle, ~100 cycles).
    CHECK(zeroCrossings > 100);
    CHECK(zeroCrossings < 300);
}

TEST_CASE("AudioCapture resets cleanly when the source sample rate changes mid-stream", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(44100.0);

    std::vector<float> block(512, 0.5f);
    const float* channelData[2] { block.data(), block.data() };

    capture.pushAudioBlock(channelData, 2, 512, 48000.0);
    readAll(capture); // drain

    // Switching source rate shouldn't crash or corrupt subsequent output.
    capture.pushAudioBlock(channelData, 2, 512, 44100.0);
    auto result = readAll(capture);

    REQUIRE(result.left.size() > 0);
    for (float sample : result.left)
        CHECK(std::isfinite(sample));
}

TEST_CASE("AudioCapture duplicates mono input to both channels", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> mono(256);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = (float) i / 256.0f;

    const float* channelData[1] { mono.data() };
    capture.pushAudioBlock(channelData, 1, 256, 48000.0);

    auto result = readAll(capture);
    REQUIRE(result.left.size() == result.right.size());
    for (size_t i = 0; i < result.left.size(); ++i)
        CHECK(result.left[i] == Catch::Approx(result.right[i]));
}

TEST_CASE("AudioCapture::getCurrentLatencyMs returns 0 before any capture", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    juce::AudioBuffer<float> buffer(2, 256);
    capture.process(buffer);
    CHECK(capture.getCurrentLatencyMs() == Catch::Approx(0.0).margin(0.0));
}

TEST_CASE("AudioCapture::getCurrentLatencyMs is small right after an immediate drain", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    auto result = readAll(capture);
    REQUIRE(result.left.size() > 0);

    const double latencyMs = capture.getCurrentLatencyMs();
    CHECK(latencyMs >= 0.0);
    CHECK(latencyMs < 50.0);
}

TEST_CASE("AudioCapture::getCurrentLatencyMs reflects real elapsed time for queued backlog", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    // Deliberately don't drain yet - let the pushed block sit in the FIFO for a known duration
    // before it's read, so getCurrentLatencyMs() has genuine dwell time to report. Once fully
    // drained and idle, latency correctly settles back to 0 instead - see "settles latency back
    // to 0 once fully drained and idle" below - so that scenario can't be used to test this one.
    juce::Thread::sleep(50);

    auto result = readAll(capture);
    REQUIRE(result.left.size() > 0);

    const double latencyMs = capture.getCurrentLatencyMs();
    CHECK(std::isfinite(latencyMs));
    CHECK(latencyMs >= 30.0); // margin under the 50ms sleep to tolerate scheduler slop
}

TEST_CASE("AudioCapture::getCurrentLatencyMs settles back to 0 once fully drained and idle", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    auto result = readAll(capture);
    REQUIRE(result.left.size() > 0);

    // Regression guard: once fully caught up, idling (nothing new produced or consumed) must not
    // make latency keep dwelling against an increasingly stale production timestamp.
    juce::Thread::sleep(50);
    juce::AudioBuffer<float> emptyBuffer(2, 1);
    capture.process(emptyBuffer);
    CHECK(capture.getCurrentLatencyMs() == Catch::Approx(0.0).margin(0.0));
}

TEST_CASE("AudioCapture::getCurrentLatencyMs does not grow across repeated device-switch prepare() calls", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };
    juce::AudioBuffer<float> buffer(2, 1 << 16);

    // Regression guard for the original bug: switching devices (re-invoking prepare() on an
    // already-live pipeline) used to compound leftover backlog across every switch, so latency
    // kept climbing a little more each time instead of settling back down.
    for (int i = 0; i < 5; ++i)
    {
        capture.pushAudioBlock(channelData, 2, 256, 48000.0);
        readAll(capture);

        // Simulate a device switch mid-capture: prepare() runs again, and (as happens during the
        // old device's teardown / new device's startup) the producer keeps pushing before the
        // consumer runs again.
        capture.prepare(48000.0);
        capture.pushAudioBlock(channelData, 2, 256, 48000.0);

        capture.process(buffer); // first process() call after the switch flushes that backlog

        CHECK(capture.getCurrentLatencyMs() == Catch::Approx(0.0).margin(0.0));
    }
}

TEST_CASE("AudioCapture discards backlog accumulated during a mid-capture prepare() (device switch)", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };

    // Establish a live pipeline (some audio already captured and drained) before the
    // device-switch prepare() - the flush behaviour under test only applies once the pipeline was
    // already live, not on the very first prepare() before any capture has happened (see the
    // "reflects real elapsed time for queued backlog" test above for that case).
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);
    readAll(capture);

    capture.prepare(48000.0); // simulated device switch
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    // The very next process() call discards that backlog rather than deliver it, so the pipeline
    // starts fresh instead of reporting the device's startup gap as latency.
    juce::AudioBuffer<float> buffer(2, 1 << 16);
    const int numRead = capture.process(buffer);
    CHECK(numRead == 0);
    CHECK(capture.getCurrentLatencyMs() == Catch::Approx(0.0).margin(0.0));

    // Confirm the pipeline is genuinely still usable afterwards, not just left permanently empty.
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);
    auto result = readAll(capture);
    CHECK(result.left.size() > 0);
}

TEST_CASE("AudioCapture does not discard legitimate startup backlog on the very first prepare()", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0); // the very first prepare() - nothing captured yet, no switch happened

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    // Unlike a mid-capture device-switch prepare(), this backlog is ordinary startup latency and
    // must be delivered normally, not flushed away.
    juce::AudioBuffer<float> buffer(2, 1 << 16);
    const int numRead = capture.process(buffer);
    CHECK(numRead > 0);
}

TEST_CASE("AudioCapture two back-to-back prepare() calls before any push do not arm the backlog flush", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);
    capture.prepare(48000.0); // still nothing ever pushed - totalBlocksReceived is still 0

    std::vector<float> block(256, 0.1f);
    const float* channelData[2] { block.data(), block.data() };
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    juce::AudioBuffer<float> buffer(2, 1 << 16);
    const int numRead = capture.process(buffer);
    CHECK(numRead > 0); // proves the guard is about "any block ever pushed", not "prepare() called more than once"
}

TEST_CASE("AudioCapture::getCurrentLatencyMs stays finite and non-negative across ring wraparound", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> block(64, 0.1f);
    const float* channelData[2] { block.data(), block.data() };

    // More iterations than the ring's fixed capacity (256), to exercise wraparound.
    for (int i = 0; i < 300; ++i)
    {
        capture.pushAudioBlock(channelData, 2, 64, 48000.0);
        readAll(capture);

        const double latencyMs = capture.getCurrentLatencyMs();
        INFO("iteration " << i);
        CHECK(std::isfinite(latencyMs));
        CHECK(latencyMs >= 0.0);
    }
}

TEST_CASE("AudioCapture doesn't crash when pushed more samples than its internal capacity", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    constexpr int hugeBlock = 1 << 15; // bigger than AudioCapture's internal maxPendingSamples (1<<14)
    std::vector<float> left(hugeBlock, 0.1f), right(hugeBlock, -0.1f);
    const float* channelData[2] { left.data(), right.data() };

    REQUIRE_NOTHROW(capture.pushAudioBlock(channelData, 2, hugeBlock, 48000.0));

    auto result = readAll(capture);
    for (float sample : result.left)
        CHECK(std::isfinite(sample));
}

TEST_CASE("AudioCapture pending-buffer overflow silently drops only the excess, in order", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0); // 1:1 ratio - no resampling to muddy the exact-index check below

    constexpr int hugeBlock = 20000; // > maxPendingSamples (1<<14 == 16384)
    std::vector<float> ramp((size_t) hugeBlock);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float) i; // index-identifiable content

    const float* channelData[2] { ramp.data(), ramp.data() };
    capture.pushAudioBlock(channelData, 2, hugeBlock, 48000.0);

    auto result = readAll(capture);

    // Only indices 0..16383 could ever have been appended into the fixed pending arrays; indices
    // 16384..19999 were dropped at push time and must never appear anywhere in the output.
    REQUIRE(result.left.size() <= 16384);
    for (float sample : result.left)
        CHECK(sample < 16384.0f);
}

TEST_CASE("AudioCapture diagnostic counters track an exact scripted sequence", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> loud(128, 0.9f);
    std::vector<float> quiet(64, 0.1f);
    const float* loudChannels[2] { loud.data(), loud.data() };
    const float* quietChannels[2] { quiet.data(), quiet.data() };

    capture.pushAudioBlock(loudChannels, 2, 128, 48000.0);
    capture.pushAudioBlock(quietChannels, 2, 64, 48000.0);
    capture.pushAudioBlock(quietChannels, 2, 64, 48000.0);

    CHECK(capture.totalBlocksReceived.load() == 3);
    // Last-pushed-block-only semantics, not a running max: loud was pushed first, quiet last.
    CHECK(capture.lastWrittenBlockPeak.load() == Catch::Approx(0.1f));

    juce::AudioBuffer<float> firstRead(2, 100);
    const int firstNumRead = capture.process(firstRead);
    CHECK(capture.totalProcessCalls.load() == 1);
    CHECK(capture.lastRequestedBufferSize.load() == 100);
    CHECK(capture.lastNumRead.load() == firstNumRead);
    CHECK(capture.totalSamplesRead.load() == firstNumRead);

    juce::AudioBuffer<float> secondRead(2, 1 << 16);
    const int secondNumRead = capture.process(secondRead);
    CHECK(capture.totalProcessCalls.load() == 2);
    CHECK(capture.lastRequestedBufferSize.load() == 1 << 16);
    CHECK(capture.lastNumRead.load() == secondNumRead);
    CHECK(capture.totalSamplesRead.load() == firstNumRead + secondNumRead);
}

TEST_CASE("AudioCapture::process leaves the untouched underrun tail alone", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    constexpr float sentinel = 0.42f;
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, sentinel);

    std::vector<float> block(64, 0.1f); // far fewer samples ready than the 512 requested
    const float* channelData[2] { block.data(), block.data() };
    capture.pushAudioBlock(channelData, 2, 64, 48000.0);

    const int numRead = capture.process(buffer);
    REQUIRE(numRead < buffer.getNumSamples());

    // Caller's responsibility per the documented contract: the tail beyond numRead is left
    // exactly as the caller supplied it, not zeroed.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = numRead; i < buffer.getNumSamples(); ++i)
        {
            INFO("channel " << ch << " sample " << i);
            // Approx(...).margin(0.0) instead of == : the tail must be untouched bit-for-bit, not
            // just close to the sentinel, but a raw == trips -Wfloat-equal inside Catch2's
            // decomposition template.
            CHECK(buffer.getSample(ch, i) == Catch::Approx(sentinel).margin(0.0));
        }
}

TEST_CASE("AudioCapture::process duplicates fifo channel R into every dest channel beyond stereo", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::vector<float> left(256, 0.7f), right(256, -0.4f); // distinguishable L != R
    const float* channelData[2] { left.data(), right.data() };
    capture.pushAudioBlock(channelData, 2, 256, 48000.0);

    juce::AudioBuffer<float> buffer(4, 256); // more than 2 destination channels
    const int numRead = capture.process(buffer);
    REQUIRE(numRead > 0);

    // Skip the interpolator warm-up region, same rationale as the 1:1 passthrough test above.
    for (int i = 8; i < numRead; ++i)
    {
        INFO("sample index " << i);
        CHECK(buffer.getSample(0, i) == Catch::Approx(0.7f).margin(0.001));
        // Channels 1, 2 and 3 all read from fifo channel R - stereo duplication, not true
        // up-mixing (AudioCapture.cpp's process() uses juce::jmin(ch, 1) as the source channel).
        CHECK(buffer.getSample(1, i) == Catch::Approx(-0.4f).margin(0.001));
        CHECK(buffer.getSample(2, i) == Catch::Approx(-0.4f).margin(0.001));
        CHECK(buffer.getSample(3, i) == Catch::Approx(-0.4f).margin(0.001));
    }
}

TEST_CASE("AudioCapture::lastReadBlockPeak measures the whole dest buffer, including an untouched underrun tail", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    constexpr float loudSentinel = 0.9f;
    juce::AudioBuffer<float> buffer(2, 512);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, loudSentinel);

    std::vector<float> quiet(64, 0.01f); // far fewer samples ready than requested - underrun
    const float* channelData[2] { quiet.data(), quiet.data() };
    capture.pushAudioBlock(channelData, 2, 64, 48000.0);

    const int numRead = capture.process(buffer);
    REQUIRE(numRead < buffer.getNumSamples());

    // lastReadBlockPeak is computed over the whole dest buffer (via getMagnitude across its full
    // length), so the untouched loud leftover tail dominates it - not the quiet content actually
    // written by this call. A real, easy-to-regress quirk worth pinning explicitly.
    CHECK(capture.lastReadBlockPeak.load() == Catch::Approx(loudSentinel));
}

TEST_CASE("AudioCapture survives a real producer/consumer thread race without corrupting output", "[AudioCapture]")
{
    acdsp::AudioCapture capture;
    capture.prepare(48000.0);

    std::atomic<bool> stop { false };
    std::atomic<bool> sawBadSample { false };

    std::thread producer([&]
    {
        std::vector<float> block(37, 0.3f);
        const float* channelData[2] { block.data(), block.data() };
        while (! stop.load(std::memory_order_relaxed))
        {
            capture.pushAudioBlock(channelData, 2, (int) block.size(), 44100.0);
            std::this_thread::yield();
        }
    });

    juce::AudioBuffer<float> scratch(2, 512);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 200.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        capture.process(scratch);
        for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
            for (int i = 0; i < scratch.getNumSamples(); ++i)
                if (! std::isfinite(scratch.getSample(ch, i)))
                    sawBadSample.store(true, std::memory_order_relaxed);
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();

    // Only post-join invariants matter here - real value is running this under ThreadSanitizer.
    CHECK_FALSE(sawBadSample.load());
    CHECK(std::isfinite(capture.getCurrentLatencyMs()));
    CHECK(capture.getCurrentLatencyMs() >= 0.0);
}
