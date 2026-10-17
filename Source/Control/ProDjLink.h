#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace pl
{

/** Pioneer Pro DJ Link: decks, tempo, position, track metadata and waveforms from
    CDJs / XDJs on the network. Phase joins as a virtual CDJ (keep-alives on port
    50000), receives status (50002) and beats / CDJ-3000 positions (50001), and asks
    the players' database servers (TCP) for metadata, beat grids and waveforms.
    Protocol from Deep Symmetry's DJ Link analysis (djl-analysis.deepsymmetry.org). */
namespace prodj
{
    constexpr int announcePort = 50000, beatPort = 50001, statusPort = 50002;
    extern int dbQueryPort;                  // 12523 (changeable for tests)

    bool hasMagic (const void* data, int size);
    float pitchToPercent (juce::uint32 raw);          // 0x100000 = 0 %

    //==========================================================================
    struct Device
    {
        int number = 0;
        juce::String name, ip;
        bool isMixer = false;
        double lastSeen = 0.0;
    };
    juce::MemoryBlock buildKeepAlive (int deviceNumber, const juce::String& name,
                                      const juce::MACAddress&, const juce::String& ip, int peers);
    bool parseKeepAlive (const void* data, int size, Device& out);

    struct Status
    {
        int number = 0;
        juce::String name;
        bool active = false;
        int trackPlayer = 0, slot = 0, trackType = 0;
        juce::uint32 rekordboxId = 0;
        int playState = 0;
        bool playing = false, master = false, sync = false, onAir = false;
        float pitch = 0.0f;                  // percent
        float trackBpm = -1.0f;              // the track's BPM (before pitch), -1 = none
        juce::int64 beat = -1;
        int beatInBar = 0;
    };
    bool parseStatus (const void* data, int size, Status& out);       // CDJ status, port 50002

    struct Beat
    {
        int number = 0;
        float pitch = 0.0f, trackBpm = 0.0f;
        int beatInBar = 0;
        int nextBeatMs = 0;
    };
    bool parseBeat (const void* data, int size, Beat& out);           // type 0x28, port 50001

    struct Position
    {
        int number = 0;
        int trackLengthSec = 0;
        int playheadMs = 0;
        float pitch = 0.0f, bpm = 0.0f;
    };
    bool parsePosition (const void* data, int size, Position& out);   // CDJ-3000 type 0x0b, port 50001

    //==========================================================================
    // dbserver messages (TCP)
    struct Field
    {
        enum class Kind { Number, Blob, String } kind = Kind::Number;
        juce::uint32 number = 0;
        int size = 4;                         // number fields: 1, 2 or 4 bytes
        juce::MemoryBlock blob;
        juce::String text;

        static Field num (juce::uint32 v, int bytes = 4) { Field f; f.number = v; f.size = bytes; return f; }
        static Field str (const juce::String& s) { Field f; f.kind = Kind::String; f.text = s; return f; }
        static Field data (const juce::MemoryBlock& b) { Field f; f.kind = Kind::Blob; f.blob = b; return f; }
    };

    struct Message
    {
        juce::uint32 txId = 0;
        int type = 0;
        std::vector<Field> args;

        juce::uint32 numberArg (size_t i) const { return i < args.size() ? args[i].number : 0; }
        juce::String stringArg (size_t i) const { return i < args.size() ? args[i].text : juce::String(); }
        const juce::MemoryBlock* blobArg() const
        {
            for (auto& a : args) if (a.kind == Field::Kind::Blob) return &a.blob;
            return nullptr;
        }
    };

    void writeField (juce::MemoryOutputStream&, const Field&);
    juce::MemoryBlock encode (const Message&);
    /** Decodes one message from the start of `data`. Returns bytes used, 0 if incomplete, -1 if invalid. */
    int decode (const void* data, size_t size, Message& out);

    juce::uint32 dmst (int device, int menu, int slot, int trackType);
    juce::uint32 fourcc (const char* tag);    // "PWV5" -> number (as the dbserver wants it)

    //==========================================================================
    // track data
    struct TrackInfo
    {
        juce::String title, artist, album, key, genre;
        int durationSec = 0;
        float bpm = 0.0f;
    };

    struct Waveform
    {
        std::vector<juce::uint8> height;      // 0..31
        std::vector<juce::uint32> colour;     // ARGB per column
        int perSecond = 0;                    // detail: 150 columns per second; preview: 0 (whole track)
        bool empty() const { return height.empty(); }
    };

    struct BeatGrid
    {
        std::vector<int> timeMs;              // beat n (1-based) is at timeMs[n - 1]
        std::vector<int> beatInBar;
        std::vector<float> bpm;
        bool empty() const { return timeMs.empty(); }
    };

    Waveform decodePreview (const juce::MemoryBlock&);           // 2004 -> 4402 (400 columns)
    Waveform decodeDetail (const juce::MemoryBlock&);            // 2904 -> 4a02 (150 / s, blue -> white)
    Waveform decodeColourDetail (const juce::MemoryBlock&);      // 2c04 PWV5 (150 / s, RGB)
    BeatGrid decodeBeatGrid (const juce::MemoryBlock&);          // 2204 -> 4602
    std::vector<std::pair<int, juce::String>> parseMenuItems (const std::vector<Message>&);   // (item type, label)
    TrackInfo trackInfoFrom (const std::vector<Message>& menuItems);

    struct Track
    {
        TrackInfo info;
        Waveform preview, detail;
        BeatGrid grid;
        juce::String error;
    };

    /** One dbserver connection to a player (blocking calls: use from a worker thread). */
    class DbClient
    {
    public:
        /** Connects and does the greeting + setup. `asDevice` must be a player number 1-4 present on the network. */
        juce::String connect (const juce::String& ip, int asDevice, int timeoutMs = 3000);
        void close() { socket.reset(); }

        /** Everything for one track; fills what it can and notes errors. */
        Track fetchTrack (int slot, int trackType, juce::uint32 rekordboxId);

        bool request (int type, std::vector<Field> args, Message& reply);
        bool renderMenu (int slot, int trackType, int count, std::vector<Message>& items);

    private:
        bool readExact (void* dest, int bytes);
        bool readMessage (Message&);
        std::unique_ptr<juce::StreamingSocket> socket;
        juce::MemoryBlock pending;
        juce::uint32 nextTx = 1;
        int device = 0;
        int timeout = 3000;
    };
}

//==============================================================================
/** The DJ Link manager: virtual CDJ, deck states and the track data fetcher. */
class ProDjLink : private juce::Thread
{
public:
    ProDjLink();
    ~ProDjLink() override;

    struct Settings
    {
        bool enabled = false;
        juce::String interfaceIp;             // empty = first local address
        int deviceNumber = 0;                 // 0 = automatic (a free player number 1-4 when there is one)
        bool followMasterTempo = true;        // the Dimmer FX speed follows the master deck
    };
    void setSettings (const Settings&);
    Settings getSettings() const;

    struct Deck
    {
        int number = 0;
        juce::String name, ip;
        prodj::Status status;
        double statusTime = 0.0;
        bool hasPosition = false;             // CDJ-3000 position packets
        prodj::Position position;
        double positionTime = 0.0;
        double beatTime = 0.0;                // when the current beat started (local clock)
        juce::int64 beatAtTime = -1;
        std::shared_ptr<const prodj::Track> track;
        juce::String trackState;              // "Loading track data..." / error / ""
        double retryAt = 0.0;                 // after a failed fetch: when to try again

        /** Effective tempo (track BPM x pitch), 0 if unknown. */
        float bpm() const;
        /** Estimated playhead in ms at local time `now` (seconds), -1 if unknown. */
        double playheadMs (double now) const;
        int trackLengthMs() const;
    };

    std::vector<Deck> decks() const;
    std::vector<prodj::Device> devices() const;
    juce::String statusText() const;
    int ourNumber() const { return own.load(); }
    /** The master deck's effective tempo, 0 if none. */
    float masterBpm() const;
    /** Counts master beats (for beat-synced things). */
    juce::int64 masterBeatCount() const { return masterBeats.load(); }

    static double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    /** For tests: feed a packet as if it arrived on `port` from `ip`. */
    void handlePacket (int port, const void* data, int size, const juce::String& ip);

private:
    void run() override;
    void sendKeepAlive();
    void chooseNumber();
    void requestTrack (int deckNumber, const prodj::Status&);

    class Fetcher;
    std::unique_ptr<Fetcher> fetcher;

    mutable juce::CriticalSection lock;
    Settings settings;
    std::map<int, Deck> deckMap;
    std::map<juce::String, prodj::Device> deviceMap;          // key: ip + number
    std::map<juce::String, std::shared_ptr<const prodj::Track>> cache;   // "player:slot:id"
    juce::String status;
    std::atomic<int> own { 0 };
    std::atomic<juce::int64> masterBeats { 0 };
    double startedAt = 0.0, lastKeepAlive = 0.0;
    std::atomic<bool> settingsChanged { true };

    std::unique_ptr<juce::DatagramSocket> announceSocket, beatSocket, statusSocket;
};

} // namespace pl
