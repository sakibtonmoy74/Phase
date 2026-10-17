#pragma once
#include <juce_core/juce_core.h>
#include "AudioReactive.h"

namespace pl
{

/** Real-time audio analysis: overall level, bass / mid / high bands and beat
    detection, with automatic gain so quiet and loud inputs both use 0..1.
    Pure DSP (no device), so it can be tested with generated signals. */
class AudioAnalysis
{
public:
    void prepare (double sampleRate);

    /** Analyse a block of mono samples; writes the results into `out`.
        Returns true if a beat started in this block. */
    bool process (const float* samples, int numSamples, AudioLevels& out);

    void setGain (float g)          { gain = juce::jlimit (0.1f, 10.0f, g); }
    float getGain() const           { return gain; }
    void setAutoGain (bool a)       { autoGain = a; }

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowPass (double sr, double f, double q);
        void highPass (double sr, double f, double q);
        void bandPass (double sr, double f, double q);
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Band
    {
        float env = 0.0f;
        void follow (float rms, float attack, float release) { env += (rms > env ? attack : release) * (rms - env); }
    };

    double sampleRate = 48000.0;
    Biquad bassFilter, midFilter, highFilter;
    Band levelBand, bassBand, midBand, highBand;
    float levelPeak = 0.01f, bandPeak = 0.01f;      // auto-gain references (bands share one, so they compare)
    float gain = 1.0f;
    bool autoGain = true;

    // beat detection on bass energy
    float bassAverage = 0.0f, beatPulse = 0.0f;
    double samplesSinceBeat = 1.0e9;
};

} // namespace pl
