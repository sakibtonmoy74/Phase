#pragma once
#include <juce_core/juce_core.h>

namespace pl::settings
{
    /** Machine settings (MIDI, audio device), kept apart from show files. */
    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Phase").getChildFile ("settings.json");
    }

    inline juce::var& root()
    {
        static juce::var r = []
        {
            auto v = juce::JSON::parse (file().loadFileAsString());
            return v.isObject() ? v : juce::var (new juce::DynamicObject());
        }();
        return r;
    }

    inline juce::var get (const char* section) { return root()[section]; }

    inline void set (const char* section, const juce::var& value)
    {
        if (auto* o = root().getDynamicObject())
            o->setProperty (section, value);
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::JSON::toString (root()));
    }
}
