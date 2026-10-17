#include "AudioAnalysis.h"

namespace pl
{

// RBJ cookbook biquads
static void rbj (float& b0, float& b1, float& b2, float& a1, float& a2,
                 double B0, double B1, double B2, double A0, double A1, double A2)
{
    b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
    a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
}

void AudioAnalysis::Biquad::lowPass (double sr, double f, double q)
{
    const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
    rbj (b0, b1, b2, a1, a2, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

void AudioAnalysis::Biquad::highPass (double sr, double f, double q)
{
    const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
    rbj (b0, b1, b2, a1, a2, (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

void AudioAnalysis::Biquad::bandPass (double sr, double f, double q)
{
    const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
    rbj (b0, b1, b2, a1, a2, al, 0, -al, 1 + al, -2 * c, 1 - al);
}

void AudioAnalysis::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    bassFilter.lowPass (sampleRate, 150.0, 0.707);
    midFilter.bandPass (sampleRate, 800.0, 0.6);
    highFilter.highPass (sampleRate, 3000.0, 0.707);
    levelBand = bassBand = midBand = highBand = Band();
    levelPeak = bandPeak = 0.01f;
    bassAverage = beatPulse = 0.0f;
    samplesSinceBeat = 1.0e9;
}

bool AudioAnalysis::process (const float* x, int n, AudioLevels& out)
{
    if (n <= 0) return false;

    double sumAll = 0, sumBass = 0, sumMid = 0, sumHigh = 0;
    for (int i = 0; i < n; ++i)
    {
        const float s = x[i];
        const float b = bassFilter.process (s), m = midFilter.process (s), h = highFilter.process (s);
        sumAll += s * s; sumBass += b * b; sumMid += m * m; sumHigh += h * h;
    }
    const float inv = 1.0f / (float) n;
    const float rmsAll = std::sqrt ((float) sumAll * inv), rmsBass = std::sqrt ((float) sumBass * inv);
    const float rmsMid = std::sqrt ((float) sumMid * inv), rmsHigh = std::sqrt ((float) sumHigh * inv);

    // time constants per block, independent of block size
    const float blockSec = (float) n / (float) sampleRate;
    auto coeff = [blockSec] (float seconds) { return 1.0f - std::exp (-blockSec / seconds); };
    const float attack = coeff (0.01f), release = coeff (0.15f);
    const float peakDecay = std::exp (-blockSec / 4.0f);                // AGC reference falls over ~4 s

    // music carries most energy in the bass: weight the upper bands so all three move
    levelBand.follow (rmsAll * gain, attack, release);
    bassBand.follow  (rmsBass * gain, attack, release);
    midBand.follow   (rmsMid * gain * 2.0f, attack, release);
    highBand.follow  (rmsHigh * gain * 4.0f, attack, release);

    if (autoGain)
    {
        levelPeak = juce::jmax (levelPeak * peakDecay, levelBand.env, 0.005f);
        bandPeak  = juce::jmax (juce::jmax (bandPeak * peakDecay, bassBand.env), juce::jmax (midBand.env, highBand.env, 0.005f));
        out.level = juce::jlimit (0.0f, 1.0f, levelBand.env / levelPeak);
        out.bass  = juce::jlimit (0.0f, 1.0f, bassBand.env / bandPeak);
        out.mid   = juce::jlimit (0.0f, 1.0f, midBand.env / bandPeak);
        out.high  = juce::jlimit (0.0f, 1.0f, highBand.env / bandPeak);
    }
    else
    {
        out.level = juce::jlimit (0.0f, 1.0f, levelBand.env * 3.0f);
        out.bass  = juce::jlimit (0.0f, 1.0f, bassBand.env * 3.0f);
        out.mid   = juce::jlimit (0.0f, 1.0f, midBand.env * 3.0f);
        out.high  = juce::jlimit (0.0f, 1.0f, highBand.env * 3.0f);
    }

    // beat: bass energy jumps well above its recent average (min 250 ms apart)
    const float energy = rmsBass * rmsBass;
    samplesSinceBeat += n;
    bool beat = false;
    if (energy > bassAverage * 2.0f && energy > 1.0e-6f && samplesSinceBeat > sampleRate * 0.25)
    {
        beat = true;
        samplesSinceBeat = 0;
        beatPulse = 1.0f;
    }
    bassAverage += coeff (0.6f) * (energy - bassAverage);
    beatPulse *= std::exp (-blockSec / 0.12f);
    out.beat = beatPulse;
    return beat;
}

} // namespace pl
