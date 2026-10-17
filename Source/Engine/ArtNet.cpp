#include <cstring>
#include "ArtNet.h"

namespace pl
{

namespace artnet
{
    static const char header[8] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0 };
    constexpr juce::uint16 opPoll = 0x2000, opPollReply = 0x2100, opDmx = 0x5000;
    constexpr juce::uint8 protVerHi = 0, protVerLo = 14;

    juce::MemoryBlock buildArtDmx (int portAddress, juce::uint8 sequence,
                                   const juce::uint8* data, int length)
    {
        length = juce::jlimit (2, 512, length);
        if (length % 2 != 0) ++length;

        juce::MemoryBlock mb ((size_t) (18 + length), true);
        auto* p = static_cast<juce::uint8*> (mb.getData());

        std::memcpy (p, header, 8);
        p[8]  = (juce::uint8) (opDmx & 0xff);         // OpCode, little-endian
        p[9]  = (juce::uint8) (opDmx >> 8);
        p[10] = protVerHi;
        p[11] = protVerLo;
        p[12] = sequence;
        p[13] = 0;                                    // Physical
        portAddress = juce::jlimit (0, maxPortAddress, portAddress);
        p[14] = (juce::uint8) (portAddress & 0xff);   // SubUni
        p[15] = (juce::uint8) ((portAddress >> 8) & 0x7f); // Net
        p[16] = (juce::uint8) (length >> 8);          // Length, big-endian
        p[17] = (juce::uint8) (length & 0xff);
        if (data != nullptr)
            std::memcpy (p + 18, data, (size_t) juce::jmin (length, 512));
        return mb;
    }

    juce::MemoryBlock buildArtPoll()
    {
        juce::MemoryBlock mb (14, true);
        auto* p = static_cast<juce::uint8*> (mb.getData());
        std::memcpy (p, header, 8);
        p[8]  = (juce::uint8) (opPoll & 0xff);
        p[9]  = (juce::uint8) (opPoll >> 8);
        p[10] = protVerHi;
        p[11] = protVerLo;
        p[12] = 0;   // Flags
        p[13] = 0;   // DiagPriority
        return mb;
    }

    int opCodeOf (const void* data, int size)
    {
        if (size < 10 || std::memcmp (data, header, 8) != 0)
            return -1;
        auto* p = static_cast<const juce::uint8*> (data);
        return p[8] | (p[9] << 8);
    }

    static juce::String readString (const juce::uint8* p, int maxLen)
    {
        int len = 0;
        while (len < maxLen && p[len] != 0) ++len;
        return juce::String::fromUTF8 (reinterpret_cast<const char*> (p), len).trim();
    }

    bool parsePollReply (const void* data, int size, NodeInfo& out)
    {
        if (opCodeOf (data, size) != opPollReply || size < 108)
            return false;
        auto* p = static_cast<const juce::uint8*> (data);
        out.ip = juce::String (p[10]) + "." + juce::String (p[11]) + "."
               + juce::String (p[12]) + "." + juce::String (p[13]);
        out.shortName = readString (p + 26, 18);
        out.longName  = readString (p + 44, 64);
        out.numPorts  = size >= 174 ? ((p[172] << 8) | p[173]) : 0;
        out.outputs.clear();
        if (size >= 194)
            for (int i = 0; i < juce::jmin (4, out.numPorts); ++i)
                if ((p[174 + i] & 0x80) != 0)                         // port can output DMX
                    out.outputs.push_back (((p[18] & 0x7f) << 8) | ((p[19] & 0x0f) << 4) | (p[190 + i] & 0x0f));
        return true;
    }
}

//==============================================================================
juce::var ArtNetOutput::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", enabled);
    o->setProperty ("localIp", localIp);
    o->setProperty ("broadcast", broadcast);
    o->setProperty ("nodeIp", nodeIp);
    o->setProperty ("startUniverse", startUniverse);
    o->setProperty ("universes", universes);
    o->setProperty ("artNetStart", artNetStart);
    return juce::var (o);
}

ArtNetOutput ArtNetOutput::fromVar (const juce::var& v)
{
    ArtNetOutput o;
    o.enabled = (bool) v.getProperty ("enabled", true);
    o.localIp = v["localIp"].toString();
    o.broadcast = (bool) v.getProperty ("broadcast", true);
    o.nodeIp = v["nodeIp"].toString();
    o.startUniverse = juce::jmax (1, (int) v.getProperty ("startUniverse", 1));
    o.universes = juce::jlimit (0, 32768, (int) v.getProperty ("universes", 1));
    o.artNetStart = juce::jlimit (-1, artnet::maxPortAddress, (int) v.getProperty ("artNetStart", -1));
    return o;
}

std::vector<NetSettings::Destination> NetSettings::destinationsFor (int universe) const
{
    std::vector<Destination> out;
    for (auto& o : outputs)
    {
        if (! o.enabled || ! o.covers (universe)) continue;
        if (! o.broadcast && o.nodeIp.trim().isEmpty()) continue;
        const int pa = o.portAddressFor (universe);
        if (pa < 0 || pa > artnet::maxPortAddress) continue;
        out.push_back ({ o.localIp, o.broadcast ? juce::String() : o.nodeIp.trim(), pa });
    }
    return out;
}

juce::var NetSettings::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", enabled);
    o->setProperty ("rateHz", rateHz);
    juce::Array<juce::var> list;
    for (auto& out : outputs) list.add (out.toVar());
    o->setProperty ("outputs", list);
    return juce::var (o);
}

void NetSettings::fromVar (const juce::var& v)
{
    if (! v.isObject()) return;
    enabled = v.getProperty ("enabled", true);
    rateHz  = juce::jlimit (1, 60, (int) v.getProperty ("rateHz", 40));
    if (auto* list = v["outputs"].getArray())
    {
        outputs.clear();
        for (auto& x : *list) outputs.push_back (ArtNetOutput::fromVar (x));
        return;
    }
    // v0.8 and older: one interface, broadcast or one node, every universe from a base Port-Address
    ArtNetOutput o;
    o.localIp = v["localIp"].toString();
    o.broadcast = ! (bool) v["unicast"];
    o.nodeIp = v["unicastIp"].toString();
    o.startUniverse = 1;
    o.universes = 0;
    o.artNetStart = juce::jlimit (0, artnet::maxPortAddress, (int) v["artNetBase"]);
    outputs = { o };
}

//==============================================================================
ArtNetSender::ArtNetSender() = default;

ArtNetSender::Port& ArtNetSender::portFor (const juce::String& localIp)
{
    auto it = ports.find (localIp);
    if (it != ports.end()) return it->second;
    Port p;
    p.socket = std::make_unique<juce::DatagramSocket> (true);
    bool bound = localIp.isNotEmpty() && p.socket->bindToPort (0, localIp);
    if (! bound) p.socket->bindToPort (0);
    p.broadcastIp = "255.255.255.255";
    if (bound)
    {
        auto bc = juce::IPAddress::getInterfaceBroadcastAddress (juce::IPAddress (localIp));
        if (! bc.isNull()) p.broadcastIp = bc.toString();
    }
    return ports.emplace (localIp, std::move (p)).first->second;
}

juce::String ArtNetSender::configure (const NetSettings& s)
{
    settings = s;
    ports.clear();
    juce::StringArray parts;
    for (auto& o : s.outputs)
    {
        if (! o.enabled) continue;
        auto& p = portFor (o.localIp);
        const juce::String range = o.universes <= 0 ? "U" + juce::String (o.startUniverse) + "+"
                                 : o.universes == 1 ? "U" + juce::String (o.startUniverse)
                                 : "U" + juce::String (o.startUniverse) + "-" + juce::String (o.startUniverse + o.universes - 1);
        parts.add (range + " -> " + (o.broadcast ? p.broadcastIp + " (broadcast)" : o.nodeIp.isNotEmpty() ? o.nodeIp : juce::String ("(no node IP yet)")));
    }
    return parts.isEmpty() ? juce::String ("No outputs: add one") : "Sending " + parts.joinIntoString (",  ");
}

int ArtNetSender::send (const UniverseMap& universes)
{
    int sent = 0;
    for (auto& [uni, data] : universes)
    {
        const auto dests = settings.destinationsFor (uni);
        if (dests.empty()) continue;
        auto& seq = sequences[uni];
        seq = (juce::uint8) (seq == 255 ? 1 : seq + 1);   // 0 means "sequencing off"
        for (auto& d : dests)
        {
            auto& p = portFor (d.localIp);
            auto packet = artnet::buildArtDmx (d.portAddress, seq, data.data(), 512);
            if (p.socket->write (d.ip.isEmpty() ? p.broadcastIp : d.ip, artnet::port, packet.getData(), (int) packet.getSize()) > 0)
                ++sent;
        }
    }
    return sent;
}

//==============================================================================
ArtNetDiscovery::ArtNetDiscovery() : juce::Thread ("ArtPoll") {}
ArtNetDiscovery::~ArtNetDiscovery() { stopThread (2000); }

void ArtNetDiscovery::poll (const juce::String& localIp)
{
    if (isThreadRunning())
        return;
    pollFrom = localIp;
    {
        const juce::ScopedLock sl (lock);
        found.clear();
    }
    startThread();
}

std::vector<artnet::NodeInfo> ArtNetDiscovery::nodes() const
{
    const juce::ScopedLock sl (lock);
    return found;
}

void ArtNetDiscovery::run()
{
    juce::DatagramSocket sock (true);
    sock.setEnablePortReuse (true);
    if (! sock.bindToPort (artnet::port))
        return;

    juce::String bc = "255.255.255.255";
    if (pollFrom.isNotEmpty())
    {
        auto addr = juce::IPAddress::getInterfaceBroadcastAddress (juce::IPAddress (pollFrom));
        if (! addr.isNull()) bc = addr.toString();
    }

    auto pollPacket = artnet::buildArtPoll();
    sock.write (bc, artnet::port, pollPacket.getData(), (int) pollPacket.getSize());

    const auto endTime = juce::Time::getMillisecondCounter() + 3000;
    juce::uint8 buffer[1024];

    while (! threadShouldExit() && juce::Time::getMillisecondCounter() < endTime)
    {
        if (sock.waitUntilReady (true, 100) != 1)
            continue;

        juce::String senderIp;
        int senderPort = 0;
        const int n = sock.read (buffer, sizeof (buffer), false, senderIp, senderPort);

        artnet::NodeInfo info;
        if (n > 0 && artnet::parsePollReply (buffer, n, info))
        {
            const juce::ScopedLock sl (lock);
            bool dup = false;
            for (auto& f : found)
                dup |= (f.ip == info.ip && f.shortName == info.shortName);
            if (! dup)
                found.push_back (info);
        }
    }
}

//==============================================================================
juce::StringArray localIPv4Addresses()
{
    juce::StringArray preferred, others, loopback;
    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        auto s = a.toString();
        if (s == "0.0.0.0" || s.isEmpty()) continue;
        if (s.startsWith ("127."))                         loopback.addIfNotAlreadyThere (s);
        else if (s.startsWith ("2.") || s.startsWith ("10.")) preferred.addIfNotAlreadyThere (s);
        else                                               others.addIfNotAlreadyThere (s);
    }
    preferred.addArray (others);
    preferred.addArray (loopback);
    return preferred;
}

//==============================================================================
ArtNetMonitor::ArtNetMonitor() : juce::Thread ("Art-Net monitor") {}
ArtNetMonitor::~ArtNetMonitor() { stop(); }

bool ArtNetMonitor::start (const juce::String& localIp)
{
    stop();
    bindIp = localIp;
    socket = std::make_unique<juce::DatagramSocket> (true);
    socket->setEnablePortReuse (true);
    // broadcasts only arrive on a socket bound to "any" on most systems: bind to any, filter nothing
    if (! socket->bindToPort (artnet::port))
    {
        socket.reset();
        return false;
    }
    startThread();
    return true;
}

void ArtNetMonitor::stop()
{
    signalThreadShouldExit();
    if (socket != nullptr) socket->shutdown();
    stopThread (1000);
    socket.reset();
}

void ArtNetMonitor::clear()
{
    const juce::ScopedLock sl (lock);
    found.clear();
    other = 0;
}

std::vector<ArtNetMonitor::Stream> ArtNetMonitor::streams() const
{
    const juce::ScopedLock sl (lock);
    std::vector<Stream> out;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (auto& [k, st] : found)
    {
        auto copy = st;
        if (now - st.lastSeen > 2.0) copy.fps = 0.0f;           // went quiet
        out.push_back (copy);
    }
    std::sort (out.begin(), out.end(), [] (const Stream& a, const Stream& b)
               { return a.portAddress != b.portAddress ? a.portAddress < b.portAddress : a.source < b.source; });
    return out;
}

bool ArtNetMonitor::parseArtDmx (const void* data, int size, int& portAddress, DmxUniverse& out, int& length)
{
    if (artnet::opCodeOf (data, size) != 0x5000 || size < 20) return false;
    auto* p = static_cast<const juce::uint8*> (data);
    portAddress = p[14] | ((p[15] & 0x7f) << 8);
    length = juce::jlimit (0, 512, (p[16] << 8) | p[17]);
    length = juce::jmin (length, size - 18);
    out.fill (0);
    std::memcpy (out.data(), p + 18, (size_t) juce::jmax (0, length));
    return true;
}

void ArtNetMonitor::run()
{
    juce::uint8 buffer[1024];
    while (! threadShouldExit() && socket != nullptr)
    {
        if (socket->waitUntilReady (true, 100) != 1) continue;
        juce::String sender;
        int senderPort = 0;
        const int n = socket->read (buffer, sizeof (buffer), false, sender, senderPort);
        if (n <= 0) continue;
        int pa = 0, len = 0;
        DmxUniverse d;
        if (! parseArtDmx (buffer, n, pa, d, len))
        {
            if (artnet::opCodeOf (buffer, n) >= 0) ++other;
            continue;
        }
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const juce::ScopedLock sl (lock);
        auto& st = found[{ sender, pa }];
        st.source = sender;
        st.portAddress = pa;
        st.data = d;
        st.length = len;
        ++st.packets;
        st.lastSeen = now;
        ++st.countThisSecond;
        if (st.secondStart <= 0.0) st.secondStart = now;
        if (now - st.secondStart >= 1.0)
        {
            st.fps = (float) st.countThisSecond / (float) (now - st.secondStart);
            st.countThisSecond = 0;
            st.secondStart = now;
        }
    }
}

} // namespace pl
