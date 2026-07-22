#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace audiocapture
{

// What kind of row an AudioOutputDeviceInfo represents in the picker list: either one of the two
// fixed sentinel choices offered alongside real devices, or an actual output-capable device.
enum class AudioOutputDeviceKind
{
    noDevice,       // user wants no audio output at all
    systemDefault,  // always follow whatever the OS reports as its current default output device
    device          // a concrete, named output-capable device
};

// One row of the "pick an audio output" list: either a sentinel (no device / follow the system
// default) or a real device, alongside its channel counts and owning driver type.
struct AudioOutputDeviceInfo
{
    AudioOutputDeviceKind kind = AudioOutputDeviceKind::noDevice;

    // Empty for the two sentinel kinds. The device's display name as reported by its
    // AudioIODeviceType, e.g. "MacBook Pro Speakers" - not necessarily unique across types.
    juce::String name;

    // Empty for the two sentinel kinds. The owning AudioIODeviceType's driver name, e.g.
    // "CoreAudio", "Windows Audio (WASAPI)", "DirectSound", "ASIO", "ALSA", "JACK" - combine with
    // `name` if you need a key that's unique across every device on the platform.
    juce::String typeName;

    int numInputChannels = 0;
    int numOutputChannels = 0;

    // True only for kind == device: whether this was its AudioIODeviceType's current default
    // output device at the moment getAllDevices() was called. Not set for the sentinel rows, and
    // not continuously updated - re-call getAllDevices() to refresh it.
    bool isDefaultOutputDevice = false;
};

// Enumerates the rows for a "select an audio output" picker (e.g. a juce::ComboBox in a JUCE
// standalone app): a "No Device" sentinel, a "Use System Device" sentinel, then every real
// output-capable device across every AudioIODeviceType available on the current platform. Not
// tied to any particular UI toolkit or host product - it just lists what's out there and lets the
// caller render/store it however it needs to.
class AudioOutputDeviceList
{
public:
    // Returns "No Device", then "Use System Device", then every current output-capable device
    // across all AudioIODeviceTypes on this platform, in the order each type/driver reports them.
    // Must be called on the JUCE message thread: this scans hardware synchronously
    // (AudioIODeviceType::scanForDevices()) and briefly probes each device's driver handle to
    // read its channel names, so it is not real-time-safe and must never be called from the audio
    // capture callback path this module's other classes run on.
    static std::vector<AudioOutputDeviceInfo> getAllDevices();

    // Renders the exact display string for a row: "No Device", "Use System Device", or
    // "<name> (<N> In, <M> Out)" - matches this module's device-picker UI convention.
    static juce::String getDisplayName(const AudioOutputDeviceInfo& device);
};

}
