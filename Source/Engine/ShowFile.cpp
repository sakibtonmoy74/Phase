#include "ShowFile.h"
#include "Gdtf.h"

namespace pl::showfile
{

juce::var toVar (DmxEngine& engine)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "Phase");
    root->setProperty ("formatVersion", formatVersion);
    root->setProperty ("saved", juce::Time::getCurrentTime().toISO8601 (true));

    {
        const juce::ScopedLock sl (engine.dataLock);

        juce::Array<juce::var> types;
        for (auto& t : engine.library.types())
            if (! t.builtIn)
                types.add (t.toVar());

        root->setProperty ("fixtureTypes", types);
        root->setProperty ("patch", engine.patch.toVar());
        root->setProperty ("executors", engine.playback.toVar());
        root->setProperty ("presets", engine.presets.toVar());
        juce::Array<juce::var> links;
        for (auto& l : engine.audioLinks) links.add (l.toVar());
        root->setProperty ("audioLinks", links);
        juce::Array<juce::var> maps;
        for (auto& m : engine.pixelMaps) maps.add (m.toVar());
        root->setProperty ("pixelMaps", maps);
    }

    root->setProperty ("network", engine.getNetSettings().toVar());

    auto* m = new juce::DynamicObject();
    m->setProperty ("grand", engine.masters.grand.load());
    m->setProperty ("dimmerSpeed", engine.masters.dimmerSpeed.load());
    m->setProperty ("movementSpeed", engine.masters.movementSpeed.load());
    m->setProperty ("size", engine.masters.size.load());
    root->setProperty ("masters", juce::var (m));
    return juce::var (root);
}

juce::String fromVar (DmxEngine& engine, const juce::var& v)
{
    if (! v.isObject() || v["app"].toString() != "Phase")
        return "Not a Phase show file";
    if ((int) v["formatVersion"] > formatVersion)
        return "This show was saved by a newer version of Phase";

    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.library.clearUserTypes();
        if (auto* types = v["fixtureTypes"].getArray())
            for (auto& t : *types)
                engine.library.addOrReplace (FixtureType::fromVar (t));

        engine.patch.fromVar (v["patch"]);
        engine.playback.fromVar (v["executors"]);
        engine.presets.fromVar (v["presets"]);
        engine.audioLinks.clear();
    engine.pixelMaps.clear();
        if (auto* links = v["audioLinks"].getArray())
            for (auto& l : *links) engine.audioLinks.push_back (AudioLink::fromVar (l));
        engine.pixelMaps.clear();
        if (auto* maps = v["pixelMaps"].getArray())
            for (auto& m : *maps) engine.pixelMaps.push_back (PixelMap::fromVar (m));
        engine.programmer.clearValues();
        engine.programmer.clearSelection();
    }
    mergeMachineLibrary (engine);

    NetSettings net = engine.getNetSettings();
    const auto keepLocalIp = net.primaryInterface();
    net.fromVar (v["network"]);
    // a saved interface may not exist on this machine: use this machine's
    const auto here = localIPv4Addresses();
    for (auto& o : net.outputs)
        if (o.localIp.isNotEmpty() && ! here.contains (o.localIp)) o.localIp = keepLocalIp;
    engine.setNetSettings (net);

    auto m = v["masters"];
    auto get = [&m] (const char* k, double def) { return juce::jlimit (0.0f, 1.0f, (float) (double) m.getProperty (k, def)); };
    engine.masters.grand         = m.isObject() ? get ("grand", 1.0)
                                                : juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("grandMaster", 1.0));
    engine.masters.dimmerSpeed   = get ("dimmerSpeed", 0.5);
    engine.masters.movementSpeed = get ("movementSpeed", 0.5);
    engine.masters.size          = get ("size", 1.0);
    return {};
}

juce::String save (DmxEngine& engine, const juce::File& file)
{
    auto json = juce::JSON::toString (toVar (engine));
    if (! file.replaceWithText (json))
        return "Couldn't write " + file.getFullPathName();
    return {};
}

juce::String load (DmxEngine& engine, const juce::File& file)
{
    if (! file.existsAsFile())
        return "File not found";
    juce::var v;
    auto result = juce::JSON::parse (file.loadFileAsString(), v);
    if (result.failed())
        return "Invalid show file: " + result.getErrorMessage();
    return fromVar (engine, v);
}

void newShow (DmxEngine& engine)
{
    {
    const juce::ScopedLock sl (engine.dataLock);
    engine.library.clearUserTypes();
    engine.patch.clear();
    engine.playback.clear();
    engine.presets.clear();
    engine.audioLinks.clear();
    engine.pixelMaps.clear();
    engine.programmer.clearValues();
    engine.programmer.clearSelection();
    }
    mergeMachineLibrary (engine);
}

//==============================================================================
juce::File machineLibraryFile()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("PHASE_LIBRARY_FILE", {});
    if (overridePath.isNotEmpty()) return juce::File (overridePath);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Phase").getChildFile ("fixture-library.phaselib");
}

static std::vector<FixtureType> readTypes (const juce::var& v)
{
    std::vector<FixtureType> out;
    const auto* arr = v["types"].getArray();
    if (arr == nullptr) arr = v["fixtureTypes"].getArray();           // a show file
    if (arr != nullptr)
        for (auto& t : *arr)
        {
            auto type = FixtureType::fromVar (t);
            if (type.id.isNotEmpty() && ! type.attributes.empty()) out.push_back (type);
        }
    return out;
}

juce::String exportLibrary (const std::vector<FixtureType>& types, const juce::File& file)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "Phase");
    root->setProperty ("fixtureLibrary", 1);
    juce::Array<juce::var> arr;
    for (auto& t : types) arr.add (t.toVar());
    root->setProperty ("types", arr);
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root))) ? juce::String()
                                                                          : "Couldn't write " + file.getFullPathName();
}

void saveMachineLibrary (DmxEngine& engine)
{
    // keep what's already in the machine library, updated with this show's types
    std::map<juce::String, FixtureType> merged;
    for (auto& t : readTypes (juce::JSON::parse (machineLibraryFile().loadFileAsString())))
        merged[t.id] = t;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& t : engine.library.types())
            if (! t.builtIn) merged[t.id] = t;
    }
    std::vector<FixtureType> list;
    for (auto& [id, t] : merged) list.push_back (t);
    exportLibrary (list, machineLibraryFile());
}

int mergeMachineLibrary (DmxEngine& engine)
{
    int added = 0;
    const auto types = readTypes (juce::JSON::parse (machineLibraryFile().loadFileAsString()));
    const juce::ScopedLock sl (engine.dataLock);
    for (auto& t : types)
        if (engine.library.find (t.id) == nullptr) { engine.library.addOrReplace (t); ++added; }
    return added;
}

juce::String importLibrary (DmxEngine& engine, const juce::File& file, int& added)
{
    if (file.isDirectory())
    {
        juce::StringArray errors;
        for (auto& f : file.findChildFiles (juce::File::findFiles, true, "*.gdtf;*.phaselib;*.phase"))
            if (auto e = importLibrary (engine, f, added); e.isNotEmpty()) errors.add (f.getFileName() + ": " + e);
        return errors.joinIntoString ("; ");
    }
    if (file.hasFileExtension ("gdtf"))
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data)) return "Couldn't read the file";
        gdtf::Description d;
        if (auto e = gdtf::read (data, d); e.isNotEmpty()) return e;
        for (auto& m : d.modes)
        {
            FixtureType t;
            if (gdtf::toFixtureType (d, m.name, t).isNotEmpty()) continue;
            const juce::ScopedLock sl (engine.dataLock);
            engine.library.addOrReplace (t);
            ++added;
        }
        return {};
    }
    const auto v = juce::JSON::parse (file.loadFileAsString());
    if (! v.isObject()) return "Not a Phase library or show";
    const auto types = readTypes (v);
    const juce::ScopedLock sl (engine.dataLock);
    for (auto& t : types) { engine.library.addOrReplace (t); ++added; }
    return types.empty() ? juce::String ("No fixture types in it") : juce::String();
}

} // namespace pl::showfile
