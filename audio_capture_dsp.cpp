#ifdef AUDIO_CAPTURE_DSP_MODULES_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "audio_capture_dsp.h"

// DSP
#include "source/dsp/LatencyMonitor.cpp"
#include "source/dsp/AudioCapture.cpp"

// Capture
#include "source/capture/ProcessAudioCapture.cpp"

#if JUCE_MAC
 #include "source/capture/native/SystemAudioTap_mac.mm"
#elif JUCE_WINDOWS
 #include "source/capture/native/SystemAudioTap_windows.cpp"
#elif JUCE_LINUX
 #include "source/capture/native/SystemAudioTap_linux.cpp"
#else
 #include "source/capture/native/SystemAudioTap_stub.cpp"
#endif
