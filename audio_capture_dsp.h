/*******************************************************************************

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 audio_capture_dsp
  vendor:             Nierika
  version:            0.1.0
  name:               Audio Capture DSP
  description:        Generic producer/consumer audio capture buffer with built-in latency monitoring, plus a macOS system-audio process tap.
  website:            https://github.com/halbehers/audio_capture_dsp
  license:            MIT
  minimumCppStandard: 20

  dependencies:       juce_core juce_audio_basics juce_events

  OSXFrameworks:      CoreAudio AudioToolbox

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#pragma once
#define AUDIO_CAPTURE_DSP_MODULES_H_INCLUDED

// JUCE
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// DSP
#include "include/dsp/LatencyMonitor.h"
#include "include/dsp/AudioCapture.h"

// Capture
#include "include/capture/SystemAudioTap.h"
#include "include/capture/ProcessAudioCapture.h"

namespace acdsp = audiocapture::dsp;
