#include "AudioInput.h"
#include "Settings.h"

namespace pl
{

AudioInput::AudioInput (DmxEngine& e) : engine (e)
{
    std::unique_ptr<juce::XmlElement> saved;
    auto state = settings::get ("audioDevice").toString();
    if (state.isNotEmpty())
        saved = juce::parseXML (state);

    deviceManager.initialise (2, 0, saved.get(), true);
    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);

    auto a = settings::get ("audioAnalysis");
    gain = juce::jlimit (0.1f, 10.0f, (float) (double) a.getProperty ("gain", 1.0));
    autoGain = (bool) a.getProperty ("autoGain", true);
}

AudioInput::~AudioInput()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioInput::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = deviceManager.createStateXml())
        settings::set ("audioDevice", xml->toString());
}

juce::String AudioInput::deviceStatus() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getName() + "  -  " + juce::String (d->getCurrentSampleRate() / 1000.0, 1) + " kHz, "
             + juce::String (d->getActiveInputChannels().countNumberOfSetBits()) + " input(s)";
    return "No audio input open";
}

void AudioInput::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    analysis.prepare (d != nullptr ? d->getCurrentSampleRate() : 48000.0);
    mono.assign ((size_t) juce::jmax (512, d != nullptr ? d->getCurrentBufferSizeSamples() * 2 : 512), 0.0f);
}

void AudioInput::audioDeviceStopped()
{
    engine.audio.level = engine.audio.bass = engine.audio.mid = engine.audio.high = engine.audio.beat = 0.0f;
}

void AudioInput::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                   float* const* outputs, int numOutputs, int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr) juce::FloatVectorOperations::clear (outputs[c], numSamples);

    if ((int) mono.size() < numSamples) return;   // never allocate on the audio thread
    juce::FloatVectorOperations::clear (mono.data(), numSamples);
    int used = 0;
    for (int c = 0; c < numInputs; ++c)
        if (inputs[c] != nullptr)
        {
            juce::FloatVectorOperations::add (mono.data(), inputs[c], numSamples);
            ++used;
        }
    if (used > 1)
        juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) used, numSamples);

    analysis.setGain (gain.load());
    analysis.setAutoGain (autoGain.load());
    if (analysis.process (mono.data(), numSamples, engine.audio))
    {
        lastBeatTime = DmxEngine::now();
        ++beatCount;
    }
}

} // namespace pl
