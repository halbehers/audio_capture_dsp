# Tests

Automated Catch2 unit/integration tests for this module, self-contained via CMake + CPM (fetches
JUCE and Catch2 - no changes needed to how a host project consumes this module via Projucer or
`juce_add_module()`).

## Running

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To reuse a local JUCE checkout instead of letting CPM clone one fresh (much faster locally):

```bash
cmake -S . -B build -DCPM_JUCE_SOURCE=/path/to/your/JUCE
```

The `ProcessAudioCapture` retry-exhaustion test takes ~16 real seconds on platforms where
`SystemAudioTap::isSupported()` is true (it polls until the full 60-retry/250ms-interval give-up
cycle completes, up to a generous 60s hard timeout to tolerate slow/throttled CI runners where
`juce::Timer` can tick noticeably slower than its nominal interval) - expect the full suite to take
~20-25s locally rather than being instant, and possibly longer on a loaded CI runner.

## What's covered automatically

- `acdsp::AudioCapture` and `acdsp::LatencyMonitor`: resampling correctness, mono duplication,
  source-rate-change resets, the backlog-flush-on-`prepare()` regression (first prepare vs.
  mid-capture device-switch prepare), pending/fifo overflow clamping, diagnostic counters,
  underrun tail preservation, >2-channel duplication, ring wraparound, and bounded SPSC
  producer/consumer stress tests (see the sanitizer note below).
- `audiocapture::SystemAudioTap` and `audiocapture::ProcessAudioCapture`: graceful no-op behavior
  on unsupported platforms/OS versions, and (on supported platforms) the full retry/give-up state
  machine exercised via the `getProcessID()` seam with zero real CoreAudio/WASAPI/PipeWire calls.
- `audiocapture::ProcessList`: real process enumeration and filtering logic on supported
  platforms; empty/no-op behavior elsewhere.
- `audiocapture::AudioOutputDeviceList`: `getDisplayName()` pure-function cases for all three
  `AudioOutputDeviceKind` values, and the `NO_DEVICE`/`SYSTEM_DEFAULT` sentinel-order contract of
  `getAllDevices()` against the real device manager.

## Optional: ThreadSanitizer

The two SPSC producer/consumer stress tests (`LatencyMonitor`, `AudioCapture`) only assert
post-join invariants (no crash, finite/non-negative values) - their real value is running under
ThreadSanitizer to catch a missing acquire/release or a genuine data race that a single
deterministic run can't surface:

```bash
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --parallel
./build-tsan/Tests/audio_capture_dsp_tests "[LatencyMonitor],[AudioCapture]"
```

## What's explicitly out of automated scope

Not automatable in CI - real hardware/OS state is required:

1. **Real tap happy path** (macOS 14.2+ CoreAudio process tap, Windows WASAPI process-loopback,
   Linux PipeWire) actually capturing another live process's real audio. To verify manually: build
   a tiny host app linking this module, start a real other process playing audio, resolve its PID
   (e.g. via `ProcessList`), call `SystemAudioTap::start()`, and confirm the callback fires with
   plausible sample rates/channel counts and non-silent data; confirm `stop()` silences it.
2. **Real `AudioOutputDeviceList::getAllDevices()` enumeration** against actual sound
   hardware/drivers, including hot-plug/unplug behavior - CI runners may have zero or unusual audio
   devices. Verify manually from a real GUI app's message thread with real interfaces
   attached/detached, and eyeball that `getDisplayName()` output reads sensibly in a real
   `juce::ComboBox`.
3. **Real `ProcessList` content** on a live desktop (specific process names/`isMainApplication`
   classification against known apps) - the automated tests only check structural properties
   against whatever the live CI environment happens to report.
4. **End-to-end latency under sustained real playback**: wire `ProcessAudioCapture` +
   `AudioCapture` + a real `AudioIODevice` output in a small demo app and confirm
   `getCurrentLatencyMs()` reports a plausible, stable number during sustained real playback - the
   automated suite only checks the math, not perceived/real-world audio latency.
