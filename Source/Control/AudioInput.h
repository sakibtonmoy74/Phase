#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Engine/DmxEngine.h"
#include "Engine/AudioAnalysis.h"

namespace pl
{

/** Sound card input → AudioAnalysis → engine.audio. Owns the device manager
    (also used later for timecode and playback routing). */
class AudioInput : public juce::AudioIODeviceCallback,
                   private juce::ChangeListener
{
public:
    explicit AudioInput (DmxEngine&);
    ~AudioInput() override;

    juce::AudioDeviceManager deviceManager;

    std::atomic<float> gain { 1.0f };
    std::atomic<bool> autoGain { true };
    std::atomic<int> beatCount { 0 };          // increments on every detected beat
    std::atomic<double> lastBeatTime { 0.0 };  // DmxEngine::now() of the last beat

    juce::String deviceStatus() const;

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    DmxEngine& engine;
    AudioAnalysis analysis;
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioInput)
};

} // namespace pl
