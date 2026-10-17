#include "GdtfShare.h"

namespace pl
{

static juce::String base()
{
    // PHASE_GDTF_SHARE_URL points the client at a test server
    const auto o = juce::SystemStats::getEnvironmentVariable ("PHASE_GDTF_SHARE_URL", {});
    return o.isNotEmpty() ? o : juce::String ("https://gdtf-share.com/apis/public/");
}

juce::String GdtfShare::cookiesFrom (const juce::StringPairArray& headers)
{
    juce::StringArray pairs;
    for (int i = 0; i < headers.size(); ++i)
    {
        if (! headers.getAllKeys()[i].equalsIgnoreCase ("set-cookie")) continue;
        // several cookies may arrive joined; keep each name=value, skip the attributes
        for (auto& part : juce::StringArray::fromTokens (headers.getAllValues()[i], ";,\n", "\""))
        {
            const auto t = part.trim();
            if (! t.containsChar ('=')) continue;
            const auto name = t.upToFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
            if (name == "expires" || name == "path" || name == "domain" || name == "max-age" || name == "samesite")
                continue;
            if (name.isEmpty() || name.containsChar (' ')) continue;
            pairs.addIfNotAlreadyThere (t);
        }
    }
    return pairs.joinIntoString ("; ");
}

juce::String GdtfShare::login (const juce::String& user, const juce::String& password)
{
    // GDTF Share takes the login as a normal form post (user=...&password=...);
    // a JSON body is tried as well in case the server expects that instead
    struct Attempt { bool json; };
    juce::String lastError;
    for (auto attempt : { Attempt { false }, Attempt { true } })
    {
        juce::URL url (base() + "login.php");
        juce::String headersOut;
        if (attempt.json)
        {
            auto* body = new juce::DynamicObject();
            body->setProperty ("user", user);
            body->setProperty ("password", password);
            url = url.withPOSTData (juce::JSON::toString (juce::var (body), true));
            headersOut = "Content-Type: application/json";
        }
        else
        {
            url = url.withParameter ("user", user).withParameter ("password", password);
        }

        juce::StringPairArray headers;
        int status = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                 .withExtraHeaders (headersOut)
                                                 .withConnectionTimeoutMs (20000)
                                                 .withResponseHeaders (&headers)
                                                 .withStatusCode (&status));
        if (stream == nullptr)
            return "Can't reach gdtf-share.com (check the internet connection, or a firewall blocking Phase)";
        const auto text = stream->readEntireStreamAsString();
        const auto reply = juce::JSON::parse (text);
        if (status == 200 && (bool) reply.getProperty ("result", false))
        {
            cookie = cookiesFrom (headers);
            loggedIn = true;
            // on macOS the system also keeps the session cookie itself, so an empty
            // header list is not fatal: the next request will tell
            return {};
        }
        lastError = reply.getProperty ("error", juce::String()).toString();
        if (lastError.isEmpty())
            lastError = "HTTP " + juce::String (status) + ": " + text.substring (0, 160).trim();
        if (status != 400 && status != 415 && status != 0 && reply.isObject())
            break;                                   // a real answer (wrong password...): don't retry
    }
    cookie.clear();
    loggedIn = false;
    return "Login failed - " + lastError + "  (use your GDTF Share user name, not the e-mail)";
}

juce::String GdtfShare::parseList (const juce::String& text, std::vector<Entry>& out)
{
    const auto reply = juce::JSON::parse (text);
    if (! (bool) reply.getProperty ("result", false))
        return reply.getProperty ("error", "Unexpected reply from GDTF Share").toString();
    out.clear();
    if (auto* list = reply["list"].getArray())
        for (auto& v : *list)
        {
            Entry e;
            e.rid = (int) v["rid"];
            e.manufacturer = v["manufacturer"].toString();
            e.fixture = v["fixture"].toString();
            e.revision = v["revision"].toString();
            e.creator = v["creator"].toString();
            e.uploader = v["uploader"].toString();
            e.rating = (float) (double) v.getProperty ("rating", 0.0);
            e.lastModified = (juce::int64) v.getProperty ("lastModified", 0);
            if (auto* modes = v["modes"].getArray())
                for (auto& m : *modes)
                    e.modes.add (m["name"].toString() + " (" + m["dmxfootprint"].toString() + " ch)");
            if (e.rid > 0) out.push_back (e);
        }
    return {};
}

juce::String GdtfShare::fetchList (std::vector<Entry>& out, juce::String* rawJson)
{
    if (! loggedIn) return "Log in first";
    int status = 0;
    auto stream = juce::URL (base() + "getList.php")
        .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                .withExtraHeaders (cookieHeader())
                                .withConnectionTimeoutMs (30000)
                                .withStatusCode (&status));
    if (stream == nullptr) return "Can't reach gdtf-share.com";
    const auto text = stream->readEntireStreamAsString();
    if (status == 401) { cookie.clear(); loggedIn = false; return "GDTF Share didn't accept the session (expired?): log in again"; }
    if (rawJson != nullptr) *rawJson = text;
    return parseList (text, out);
}

juce::String GdtfShare::download (int rid, juce::MemoryBlock& out)
{
    if (! loggedIn) return "Log in first";
    int status = 0;
    auto stream = juce::URL (base() + "downloadFile.php").withParameter ("rid", juce::String (rid))
        .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                .withExtraHeaders (cookieHeader())
                                .withConnectionTimeoutMs (30000)
                                .withStatusCode (&status));
    if (stream == nullptr) return "Can't reach gdtf-share.com";
    out.reset();
    stream->readIntoMemoryBlock (out);
    if (status == 401) { cookie.clear(); loggedIn = false; return "GDTF Share didn't accept the session (expired?): log in again"; }
    const auto* b = static_cast<const char*> (out.getData());
    if (out.getSize() < 4 || b[0] != 'P' || b[1] != 'K')
        return juce::JSON::parse (out.toString()).getProperty ("error", "Download failed").toString();
    return {};
}

} // namespace pl
