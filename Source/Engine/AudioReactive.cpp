#include "AudioReactive.h"

namespace pl
{

const std::vector<AudioSource>& allAudioSources()
{
    static const std::vector<AudioSource> s { AudioSource::Level, AudioSource::Bass, AudioSource::Mid,
                                              AudioSource::High, AudioSource::Beat };
    return s;
}

juce::String audioSourceName (AudioSource s)
{
    switch (s)
    {
        case AudioSource::Level: return "Level";
        case AudioSource::Bass:  return "Bass";
        case AudioSource::Mid:   return "Mid";
        case AudioSource::High:  return "High";
        case AudioSource::Beat:  return "Beat";
    }
    return {};
}

juce::var AudioLink::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("enabled", enabled);
    o->setProperty ("source", audioSourceName (source));
    o->setProperty ("attribute", attribute);
    o->setProperty ("mode", mode == AudioLinkMode::Set ? "set" : "scale");
    o->setProperty ("min", minimum);
    o->setProperty ("max", maximum);
    o->setProperty ("group", groupNumber);
    o->setProperty ("attack", attackMs);
    o->setProperty ("release", releaseMs);
    o->setProperty ("threshold", threshold);
    o->setProperty ("curve", curve);
    juce::Array<juce::var> f;
    for (int fid : fixtures) f.add (fid);
    o->setProperty ("fixtures", f);
    return juce::var (o);
}

AudioLink AudioLink::fromVar (const juce::var& v)
{
    AudioLink l;
    l.id = (int) v["id"];
    l.name = v["name"].toString();
    l.enabled = v.getProperty ("enabled", true);
    for (auto s : allAudioSources())
        if (audioSourceName (s) == v["source"].toString()) l.source = s;
    l.attribute = v.getProperty ("attribute", "Dimmer").toString();
    l.mode = v["mode"].toString() == "set" ? AudioLinkMode::Set : AudioLinkMode::Scale;
    l.minimum = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("min", 0.0));
    l.maximum = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("max", 1.0));
    l.groupNumber = juce::jmax (0, (int) v["group"]);
    l.attackMs  = juce::jlimit (0.0f, 2000.0f, (float) (double) v.getProperty ("attack", 15.0));
    l.releaseMs = juce::jlimit (0.0f, 5000.0f, (float) (double) v.getProperty ("release", 250.0));
    l.threshold = juce::jlimit (0.0f, 0.95f, (float) (double) v.getProperty ("threshold", 0.0));
    l.curve     = juce::jlimit (0.2f, 5.0f, (float) (double) v.getProperty ("curve", 1.0));
    if (auto* f = v["fixtures"].getArray())
        for (auto& x : *f) l.fixtures.push_back ((int) x);
    return l;
}

} // namespace pl
