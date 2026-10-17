#pragma once
#include <juce_core/juce_core.h>

namespace pl
{

/** Client for the GDTF Share public API (gdtf-share.com/apis/public). Needs a
    free GDTF Share account. Calls block: run them off the message thread. */
class GdtfShare
{
public:
    struct Entry
    {
        int rid = 0;
        juce::String manufacturer, fixture, revision, creator, uploader;
        float rating = 0.0f;
        juce::int64 lastModified = 0;
        juce::StringArray modes;
    };

    /** Each returns an empty string on success, or an error to show. */
    juce::String login (const juce::String& user, const juce::String& password);
    juce::String fetchList (std::vector<Entry>& out, juce::String* rawJson = nullptr);
    juce::String download (int rid, juce::MemoryBlock& out);

    bool isLoggedIn() const { return loggedIn; }

    /** Parses a getList.php reply (also used for the cached copy). */
    static juce::String parseList (const juce::String& json, std::vector<Entry>& out);

    /** "name=value; name2=value2" from Set-Cookie headers. */
    static juce::String cookiesFrom (const juce::StringPairArray& headers);

private:
    juce::String cookie;
    bool loggedIn = false;
    juce::String cookieHeader() const { return cookie.isNotEmpty() ? "Cookie: " + cookie : juce::String(); }
};

} // namespace pl
