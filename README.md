# Audio Capture DSP - JUCE module

A JUCE module providing a generic, resampling producer/consumer audio-capture buffer with
built-in latency monitoring, plus a cross-platform system-audio process tap and a reusable
retry/lifecycle base class for tapping an arbitrary OS process's audio.

---

## Table of Contents

- [About](#about)
- [Requirements & Dependencies](#requirements--dependencies)
- [Installation](#installation)
  - [Using Projucer](#using-projucer)
  - [Using CMake](#using-cmake)
  - [Using CMake & CPM](#using-cmake--cpm)
- [Usage](#usage)
  - [`acdsp::AudioCapture`](#acdspaudiocapture)
  - [`acdsp::LatencyMonitor`](#acdsplatencymonitor)
  - [`audiocapture::SystemAudioTap`](#audiocapturesystemaudiotap)
  - [`audiocapture::ProcessAudioCapture`](#audiocaptureprocessaudiocapture)
  - [`audiocapture::ProcessList`](#audiocaptureprocesslist)
  - [`audiocapture::AudioOutputDeviceList`](#audiocaptureaudiooutputdevicelist)
- [Limitations](#limitations)

---

## About

This JUCE module factors out the audio-capture pipeline into a standalone, reusable module: given
a stream of audio blocks arriving at an arbitrary (possibly changing) sample rate, buffer and resample
them to a fixed target rate, hand them back out on demand, and track how long they sat in the pipeline.
On top of that generic buffer, it also ships the platform-specific pieces needed to source that
stream from another process's system audio output, on macOS, Windows, or Linux.

Nothing in this module is tied to any particular product - it doesn't know what it's capturing
audio *from*, only how to buffer, resample, and monitor it once it arrives.

---

## Requirements & Dependencies

**Minimum C++ Standard Version**: 20

**JUCE Modules Dependencies**:

`juce_core` `juce_audio_basics` `juce_audio_devices` `juce_events`

**Platform**: macOS, Windows, and Linux. `audiocapture::SystemAudioTap` and
`audiocapture::ProcessAudioCapture` wrap a different per-process audio-capture API on each
platform, with per-platform build requirements the module declares itself (`OSXFrameworks`,
`windowsLibs`, `linuxPackages`), so a host project just links `audiocapture::audio_capture_dsp`
and gets whichever backend matches its target:

- **macOS 14.2+** — the CoreAudio process-tap API (`OSXFrameworks: CoreAudio AudioToolbox`).
- **Windows 10 build 20348+ ("2020 Update")** — the WASAPI process-loopback API
  (`windowsLibs: Ole32`).
- **Linux** — PipeWire, matching the target process's node by PID (`linuxPackages:
  libpipewire-0.3`). Requires a running PipeWire session at runtime, not just the dev headers at
  build time; a pure ALSA/PulseAudio-only system has no equivalent capability.

On any other platform, or below the version floor above, `SystemAudioTap::isSupported()` returns
`false` and `start()` is a no-op returning `false` - see [Limitations](#limitations).

`audiocapture::ProcessList` (macOS and Windows only for now) adds two more framework/lib
requirements of its own: `OSXFrameworks: AppKit` and `windowsLibs: User32`. On any other platform
(currently including Linux) `ProcessList::isSupported()` returns `false` and `getAllProcesses()`
returns an empty list.

`audiocapture::AudioOutputDeviceList` pulls in `juce_audio_devices`, which itself adds
`OSXFrameworks: CoreMIDI` and `linuxPackages: alsa` on top of the requirements above (no extra
Windows lib - WASAPI goes through COM, already covered by `Ole32`). Unlike `ProcessList`, it's
fully cross-platform: `juce_audio_devices` wraps CoreAudio/WASAPI+DirectSound+ASIO/ALSA+JACK
behind one API, so there's no `isSupported()`/unsupported-platform case to handle.

---

## Installation

### Using Projucer

1. Clone the repository locally
```shell
git clone git@github.com:halbehers/audio_capture_dsp.git
```

2. Add the module in the Projucer app the same way you would any other local JUCE module.

### Using CMake

Just as with the Projucer, you will need a local clone of the repository.
Here you can either clone it somewhere on your machine:
```shell
git clone git@github.com:halbehers/audio_capture_dsp.git
```

Or, add it as a git submodule into your project:
```shell
cd path/to/your/projects/libs/folder
git submodule add git@github.com:halbehers/audio_capture_dsp.git
```

Then add the following to your `CMakeLists.txt` file:

```CMake
# This line must appear BEFORE juce_add_plugin
juce_add_module("path/to/audio_capture_dsp" ALIAS_NAMESPACE audiocapture)

juce_add_plugin(
    ...
)

# This line must appear BEFORE target_compile_definitions
target_link_libraries(${PROJECT_NAME} PRIVATE audiocapture::audio_capture_dsp)

target_compile_definitions(
    ...
)
```

### Using CMake & CPM

Add the following to your `CMakeLists.txt` file:

```CMake
# replace with the version you want to use
set(AUDIO_CAPTURE_DSP_VERSION 0.1.0)
# replace with your lib folder location
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libs")

cpmaddpackage(
        NAME AUDIO_CAPTURE_DSP
        GIT_TAG "v${AUDIO_CAPTURE_DSP_VERSION}"
        VERSION ${AUDIO_CAPTURE_DSP_VERSION}
        GITHUB_REPOSITORY halbehers/audio_capture_dsp
        SOURCE_DIR ${LIB_DIR}/audio_capture_dsp
)

# This line must appear BEFORE juce_add_plugin
juce_add_module("${LIB_DIR}/audio_capture_dsp" ALIAS_NAMESPACE audiocapture)

juce_add_plugin(
    ...
)

# This line must appear BEFORE target_compile_definitions
target_link_libraries(${PROJECT_NAME} PRIVATE audiocapture::audio_capture_dsp)

target_compile_definitions(
        ...
)
```

---

## Usage

Every class is included in the namespace `audiocapture`. The two generic DSP classes additionally
live under `audiocapture::dsp`, aliased `acdsp` for convenience.

### `acdsp::AudioCapture`

The core buffer. `prepare()` once (e.g. in a plugin's `prepareToPlay`), `pushAudioBlock()` from
whatever thread produces audio, `process()` once per consumer cycle (e.g. a plugin's
`processBlock`) to drain what's ready into your own output buffer:

```c++
acdsp::AudioCapture capture;
capture.prepare(sampleRate); // target sample rate you want captured audio resampled to

// producer side (any thread) - source sample rate may vary block to block
capture.pushAudioBlock(channelData, numChannels, numSamples, sourceSampleRate);

// consumer side (e.g. processBlock) - up-mixes to destBuffer's channel count
int samplesWritten = capture.process(destBuffer);

double latencyMs = capture.getCurrentLatencyMs();
```

### `acdsp::LatencyMonitor`

The dwell-time tracker `AudioCapture` uses internally. It's generic enough to use standalone for
any producer/consumer pipeline stage, not just audio:

```c++
acdsp::LatencyMonitor monitor;
monitor.recordProduced(unitsJustProduced);   // producer side
double latencyMs = monitor.consumeLatencyMs(unitsJustConsumed); // consumer side
```

### `audiocapture::SystemAudioTap`

Taps another OS process's audio output via the platform's native per-process capture API
(CoreAudio on macOS, WASAPI process loopback on Windows, PipeWire on Linux), given just a
process ID and a callback:

```c++
audiocapture::SystemAudioTap tap;

if (audiocapture::SystemAudioTap::isSupported()) // see Requirements & Dependencies for the floor on each platform
{
    tap.start(targetProcessID, [](const float* const* channelData, int numChannels, int numSamples, double sampleRate)
    {
        // forward the block wherever it needs to go, e.g. capture.pushAudioBlock(...)
    });
}

tap.stop();
```

### `audiocapture::ProcessAudioCapture`

An abstract base wiring `SystemAudioTap` to an `AudioCapture`, with a retry loop for the common
case where the target process doesn't exist yet at the moment you want to start capturing.
Subclass it and implement `getProcessID()` with whatever process-ID-resolution logic is specific
to your product:

```c++
class MyProcessCapture : public audiocapture::ProcessAudioCapture
{
public:
    explicit MyProcessCapture(acdsp::AudioCapture& destinationCapture)
        : ProcessAudioCapture(destinationCapture) {}

protected:
    bool getProcessID(int& outProcessID) override
    {
        // resolve the target process's OS process ID; return false to keep retrying
        return MyProcessLocator::find(outProcessID);
    }
};

MyProcessCapture capture { audioCapture };
capture.startCapture(); // begins polling until getProcessID() succeeds and the tap starts
capture.stopCapture();
```

### `audiocapture::ProcessList`

Enumerates OS processes for resolving a target PID - e.g. inside a `ProcessAudioCapture::getProcessID()`
override, or to drive a "pick an app to capture" UI. macOS and Windows only for now (see
[Limitations](#limitations)):

```c++
// every process the OS reports
std::vector<audiocapture::ProcessInfo> allProcesses = audiocapture::ProcessList::getAllProcesses();

// only processes that look like real user-facing apps (Firefox, QuickTime, VLC, ...), not
// background/helper processes
std::vector<audiocapture::ProcessInfo> mainApps = audiocapture::ProcessList::getMainApplicationProcesses();

// any other custom criterion
std::vector<audiocapture::ProcessInfo> matches = audiocapture::ProcessList::filterProcesses(
    [](const audiocapture::ProcessInfo& process) { return process.name.find("Firefox") != std::string::npos; });
```

### `audiocapture::AudioOutputDeviceList`

Enumerates audio output devices for a "pick an audio routing output" UI - e.g. a `juce::ComboBox`
in a JUCE standalone app. Always returns a `No Device` sentinel and a `Use System Device` sentinel
first, followed by every real output-capable device the platform reports:

```c++
std::vector<audiocapture::AudioOutputDeviceInfo> devices = audiocapture::AudioOutputDeviceList::getAllDevices();

juce::ComboBox outputDeviceBox;
for (int i = 0; i < (int) devices.size(); ++i)
    outputDeviceBox.addItem(audiocapture::AudioOutputDeviceList::getDisplayName(devices[(size_t) i]), i + 1); // JUCE item IDs are 1-based

outputDeviceBox.onChange = [&devices, &outputDeviceBox]
{
    const auto& chosen = devices[(size_t) outputDeviceBox.getSelectedItemIndex()];
    // chosen.kind distinguishes the 3 cases; chosen.typeName + chosen.name identify a real
    // device for later use with juce::AudioDeviceManager::setAudioDeviceSetup(...).
};
```

---

## Limitations

- **Version/runtime floor per platform.** `SystemAudioTap` requires macOS 14.2+, Windows 10 build
  20348+, or Linux with a reachable PipeWire session; below that floor (or on any other platform)
  `isSupported()` returns `false` and `start()` is a no-op returning `false`. `AudioCapture` and
  `LatencyMonitor` alone have no such restriction.
- **Linux needs a running PipeWire session, not just the build-time dependency.** `linuxPackages:
  libpipewire-0.3` gets the module linking; a pure ALSA/PulseAudio-only system (no PipeWire daemon
  running) will still report `isSupported() == false` at runtime.
- **The Windows and Linux `SystemAudioTap` backends are unverified on real hardware.** Both were
  written against the documented WASAPI process-loopback and PipeWire APIs but haven't been
  compiled or run on an actual Windows or Linux machine - test before relying on them in
  production, and report back any link-library or API corrections needed. The same caveat applies
  to the Windows `ProcessList` backend.
- **`ProcessList` is macOS and Windows only.** There's no OS-level signal on Linux for "is this
  process a real user-facing app" the way macOS's `NSRunningApplication.activationPolicy` or
  Windows' visible-top-level-window heuristic provide - the closest equivalent (X11/EWMH's window
  list) silently returns nothing under Wayland, the default compositor on most current Linux
  desktops, so it isn't implemented yet rather than shipping a filter that quietly doesn't work.
- **`ProcessList::getAllProcesses()` is not a hot-path operation.** Each call re-walks every OS
  process from scratch (plus, on macOS, cross-references `NSWorkspace`; on Windows, re-enumerates
  every top-level window) - call it on demand (e.g. to populate a picker UI), not per audio block.
- **`AudioOutputDeviceList::getAllDevices()` is not a hot-path operation either**, and must be
  called on the JUCE message thread. Each call rescans every `AudioIODeviceType` and briefly
  probes each device's driver handle for its channel names - call it on demand (e.g. to populate a
  picker UI), never from the audio capture callback path.
- **Fixed internal buffer sizes.** `AudioCapture`'s FIFO capacity, pending-sample buffers, and
  `LatencyMonitor`'s ring capacity are sized for typical DAW block sizes/sample rates; extreme
  values (very large block sizes, very low target sample rates) aren't validated.
- **`AudioCapture::process()` always drains from sample 0** of the destination buffer - there's no
  offset parameter for partially-filled buffers.

---

## Developers

Sebastien Halbeher (`halbehers`) - see [LICENSE](LICENSE) (MIT).
