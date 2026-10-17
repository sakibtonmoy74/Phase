#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <atomic>
#include <vector>

namespace pl
{

using DmxUniverse = std::array<juce::uint8, 512>;
using UniverseMap = std::map<int, DmxUniverse>;   // key: 1-based universe

namespace artnet
{
    constexpr int port = 6454;
    constexpr int maxPortAddress = 32767;          // 15-bit Port-Address

    /** Builds an ArtDmx (OpDmx 0x5000) packet. `length` is rounded up to even. */
    juce::MemoryBlock buildArtDmx (int portAddress, juce::uint8 sequence,
                                   const juce::uint8* data, int length);

    /** Builds an ArtPoll (OpPoll 0x2000) packet for node discovery. */
    juce::MemoryBlock buildArtPoll();

    struct NodeInfo
    {
        juce::String ip;
        juce::String shortName;
        juce::String longName;
        int numPorts = 0;
        std::vector<int> outputs;     // Port-Addresses of its outputs (from the poll reply)
    };

    bool parsePollReply (const void* data, int size, NodeInfo& out);
    int  opCodeOf (const void* data, int size);   // -1 if not Art-Net
}

/** One Art-Net output: a node (or broadcast) on an interface, and the range of
    Phase universes it gets. Add the same interface several times for several nodes. */
struct ArtNetOutput
{
    bool enabled = true;
    juce::String localIp;         // interface to send from; empty = automatic
    bool broadcast = true;        // true = directed broadcast on the interface, false = unicast to nodeIp
    juce::String nodeIp;
    int startUniverse = 1;        // first Phase universe this node gets
    int universes = 1;            // how many universes (0 = every universe from startUniverse on)
    int artNetStart = -1;         // Art-Net Port-Address of the node's first universe; -1 = startUniverse - 1

    bool covers (int universe) const
    {
        return universe >= startUniverse && (universes <= 0 || universe < startUniverse + universes);
    }
    int portAddressFor (int universe) const
    {
        return (artNetStart >= 0 ? artNetStart : startUniverse - 1) + (universe - startUniverse);
    }
    juce::var toVar() const;
    static ArtNetOutput fromVar (const juce::var&);
    bool operator== (const ArtNetOutput& o) const
    {
        return enabled == o.enabled && localIp == o.localIp && broadcast == o.broadcast && nodeIp == o.nodeIp
            && startUniverse == o.startUniverse && universes == o.universes && artNetStart == o.artNetStart;
    }
};

/** Network settings for Art-Net output. */
struct NetSettings
{
    bool enabled = true;
    int rateHz = 40;
    std::vector<ArtNetOutput> outputs { ArtNetOutput { true, {}, true, {}, 1, 0, -1 } };   // default: broadcast everything

    /** Interface used for discovery (the first output's). */
    juce::String primaryInterface() const { return outputs.empty() ? juce::String() : outputs.front().localIp; }

    /** Every output a universe goes to: (interface, destination IP or "" for broadcast, Port-Address). */
    struct Destination { juce::String localIp; juce::String ip; int portAddress = 0; };
    std::vector<Destination> destinationsFor (int universe) const;

    juce::var toVar() const;
    void fromVar (const juce::var&);
};

/** Sends universes as ArtDmx. Not thread-safe; owned by the engine thread. */
class ArtNetSender
{
public:
    ArtNetSender();

    /** Rebinds the socket. Returns a status message for the UI. */
    juce::String configure (const NetSettings&);

    /** Sends every universe in the map. Returns packets sent. */
    int send (const UniverseMap&);

private:
    struct Port { std::unique_ptr<juce::DatagramSocket> socket; juce::String broadcastIp; };
    std::map<juce::String, Port> ports;          // one socket per interface ("" = automatic)
    NetSettings settings;
    Port& portFor (const juce::String& localIp);
    std::map<int, juce::uint8> sequences;
};

/** Background ArtPoll discovery on port 6454. */
class ArtNetDiscovery : private juce::Thread
{
public:
    ArtNetDiscovery();
    ~ArtNetDiscovery() override;

    /** Sends ArtPoll to the broadcast address of `localIp` and listens ~3s. */
    void poll (const juce::String& localIp);

    std::vector<artnet::NodeInfo> nodes() const;
    bool isPolling() const { return isThreadRunning(); }

private:
    void run() override;
    juce::String pollFrom;
    mutable juce::CriticalSection lock;
    std::vector<artnet::NodeInfo> found;
};

/** Art-Net monitor (like Artnetominator): listens on port 6454 and keeps the last
    ArtDmx frame of every (source, universe) it hears, with its packet rate. */
class ArtNetMonitor : private juce::Thread
{
public:
    ArtNetMonitor();
    ~ArtNetMonitor() override;

    struct Stream
    {
        juce::String source;
        int portAddress = 0;
        DmxUniverse data {};
        int length = 0;
        juce::uint64 packets = 0;
        double lastSeen = 0.0;               // seconds (Time::getMillisecondCounterHiRes / 1000)
        float fps = 0.0f;
        int countThisSecond = 0;
        double secondStart = 0.0;
    };

    /** Starts listening on an interface ("" = all). Returns false if port 6454 can't be opened. */
    bool start (const juce::String& localIp);
    void stop();
    bool isListening() const { return isThreadRunning(); }
    std::vector<Stream> streams() const;
    void clear();
    juce::uint64 otherPackets() const { return other.load(); }

    /** Parses one ArtDmx packet into `out` (port-address, data, length). For tests too. */
    static bool parseArtDmx (const void* data, int size, int& portAddress, DmxUniverse& out, int& length);

private:
    void run() override;
    juce::String bindIp;
    mutable juce::CriticalSection lock;
    std::map<std::pair<juce::String, int>, Stream> found;
    std::atomic<juce::uint64> other { 0 };
    std::unique_ptr<juce::DatagramSocket> socket;
};

/** Lists local IPv4 addresses, with a likely Art-Net interface (2.x/10.x) first. */
juce::StringArray localIPv4Addresses();

} // namespace pl
