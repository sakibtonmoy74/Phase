#include "ProDjLink.h"
#include "Engine/ArtNet.h"      // localIPv4Addresses
#include <deque>
#include <set>

namespace pl
{
namespace prodj
{
    int dbQueryPort = 12523;

    static const juce::uint8 magicBytes[10] { 0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c };   // "Qspt1WmJOL"

    static juce::uint32 be (const juce::uint8* p, int bytes)
    {
        juce::uint32 v = 0;
        for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
        return v;
    }

    static juce::String name (const juce::uint8* p, int maxLen)
    {
        int n = 0;
        while (n < maxLen && p[n] != 0) ++n;
        return juce::String (juce::CharPointer_UTF8 ((const char*) p), (size_t) n).trim();
    }

    bool hasMagic (const void* data, int size)
    {
        return size >= 11 && std::memcmp (data, magicBytes, 10) == 0;
    }

    float pitchToPercent (juce::uint32 raw)
    {
        return (float) (((double) raw / 1048576.0 - 1.0) * 100.0);
    }

    //==========================================================================
    juce::MemoryBlock buildKeepAlive (int deviceNumber, const juce::String& deviceName,
                                      const juce::MACAddress& mac, const juce::String& ip, int peers)
    {
        juce::MemoryBlock b (0x36, true);
        auto* p = static_cast<juce::uint8*> (b.getData());
        std::memcpy (p, magicBytes, 10);
        p[0x0a] = 0x06;
        p[0x0b] = 0x00;
        const auto utf8 = deviceName.toRawUTF8();
        for (int i = 0; i < 20 && utf8[i] != 0; ++i) p[0x0c + i] = (juce::uint8) utf8[i];
        p[0x20] = 0x01;
        p[0x21] = 0x02;
        p[0x22] = 0x00; p[0x23] = 0x36;            // length
        p[0x24] = (juce::uint8) deviceNumber;
        p[0x25] = 0x01;
        for (int i = 0; i < 6; ++i) p[0x26 + i] = mac.getBytes()[i];
        auto parts = juce::StringArray::fromTokens (ip, ".", {});
        for (int i = 0; i < 4 && i < parts.size(); ++i) p[0x2c + i] = (juce::uint8) parts[i].getIntValue();
        p[0x30] = (juce::uint8) juce::jlimit (1, 255, peers);
        p[0x34] = 0x01;                            // a player
        p[0x35] = 0x00;
        return b;
    }

    bool parseKeepAlive (const void* data, int size, Device& out)
    {
        if (! hasMagic (data, size) || size < 0x36) return false;
        auto* p = static_cast<const juce::uint8*> (data);
        if (p[0x0a] != 0x06) return false;
        out.name = name (p + 0x0c, 20);
        out.number = p[0x24];
        out.ip = juce::String (p[0x2c]) + "." + juce::String (p[0x2d]) + "." + juce::String (p[0x2e]) + "." + juce::String (p[0x2f]);
        out.isMixer = p[0x34] == 0x02 || out.name.containsIgnoreCase ("DJM");
        return true;
    }

    bool parseStatus (const void* data, int size, Status& out)
    {
        if (! hasMagic (data, size) || size < 0xc8) return false;
        auto* p = static_cast<const juce::uint8*> (data);
        if (p[0x0a] != 0x0a) return false;                       // CDJ status
        out.name = name (p + 0x0b, 20);
        out.number = p[0x21];
        out.active = p[0x27] != 0;
        out.trackPlayer = p[0x28];
        out.slot = p[0x29];
        out.trackType = p[0x2a];
        out.rekordboxId = be (p + 0x2c, 4);
        out.playState = p[0x7b];
        const int f = p[0x89];
        out.playing = (f & 0x40) != 0 || out.playState == 0x03 || out.playState == 0x04;
        out.master = (f & 0x20) != 0;
        out.sync = (f & 0x10) != 0;
        out.onAir = (f & 0x08) != 0;
        out.pitch = pitchToPercent (be (p + 0x8c, 4));
        const auto bpm = be (p + 0x92, 2);
        out.trackBpm = bpm == 0xffff ? -1.0f : (float) bpm / 100.0f;
        const auto beat = be (p + 0xa0, 4);
        out.beat = beat == 0xffffffffu ? -1 : (juce::int64) beat;
        out.beatInBar = p[0xa6];
        return true;
    }

    bool parseBeat (const void* data, int size, Beat& out)
    {
        if (! hasMagic (data, size) || size < 0x60) return false;
        auto* p = static_cast<const juce::uint8*> (data);
        if (p[0x0a] != 0x28) return false;
        out.number = p[0x21];
        out.nextBeatMs = (int) be (p + 0x24, 4);
        out.pitch = pitchToPercent (be (p + 0x54, 4));
        out.trackBpm = (float) be (p + 0x5a, 2) / 100.0f;
        out.beatInBar = p[0x5c];
        return true;
    }

    bool parsePosition (const void* data, int size, Position& out)
    {
        if (! hasMagic (data, size) || size < 0x3c) return false;
        auto* p = static_cast<const juce::uint8*> (data);
        if (p[0x0a] != 0x0b) return false;
        out.number = p[0x21];
        out.trackLengthSec = (int) be (p + 0x24, 4);
        out.playheadMs = (int) be (p + 0x28, 4);
        out.pitch = (float) (juce::int32) be (p + 0x2c, 4) / 100.0f;
        const auto bpm = be (p + 0x38, 4);
        out.bpm = bpm == 0xffffffffu ? 0.0f : (float) bpm / 10.0f;
        return true;
    }

    //==========================================================================
    void writeField (juce::MemoryOutputStream& o, const Field& f)
    {
        switch (f.kind)
        {
            case Field::Kind::Number:
                if (f.size == 1)      { o.writeByte (0x0f); o.writeByte ((char) (f.number & 0xff)); }
                else if (f.size == 2) { o.writeByte (0x10); o.writeShortBigEndian ((short) (f.number & 0xffff)); }
                else                  { o.writeByte (0x11); o.writeIntBigEndian ((int) f.number); }
                break;
            case Field::Kind::Blob:
                o.writeByte (0x14);
                o.writeIntBigEndian ((int) f.blob.getSize());
                o.write (f.blob.getData(), f.blob.getSize());
                break;
            case Field::Kind::String:
            {
                o.writeByte (0x26);
                const auto utf16 = f.text.toUTF16();
                const int chars = (int) juce::CharPointer_UTF16::getBytesRequiredFor (f.text.getCharPointer()) / 2;
                o.writeIntBigEndian (chars + 1);
                auto* q = utf16.getAddress();
                for (int i = 0; i < chars; ++i) o.writeShortBigEndian ((short) q[i]);
                o.writeShortBigEndian (0);
                break;
            }
        }
    }

    juce::MemoryBlock encode (const Message& m)
    {
        juce::MemoryOutputStream o;
        writeField (o, Field::num (0x872349ae));
        writeField (o, Field::num (m.txId));
        writeField (o, Field::num ((juce::uint32) m.type, 2));
        writeField (o, Field::num ((juce::uint32) m.args.size(), 1));
        juce::MemoryBlock tags (12, true);
        for (size_t i = 0; i < m.args.size() && i < 12; ++i)
            static_cast<juce::uint8*> (tags.getData())[i] = m.args[i].kind == Field::Kind::Number ? 0x06
                                                          : m.args[i].kind == Field::Kind::String ? 0x02 : 0x03;
        writeField (o, Field::data (tags));
        juce::uint32 previous = 1;
        for (auto& a : m.args)
        {
            if (a.kind == Field::Kind::Blob && previous == 0) continue;     // empty blobs are left out
            writeField (o, a);
            previous = a.kind == Field::Kind::Number ? a.number : 1;
        }
        return o.getMemoryBlock();
    }

    // reads fields through `read (dest, bytes)` (from memory or a socket)
    using Reader = std::function<bool (void*, int)>;

    static bool readField (const Reader& read, Field& f)
    {
        juce::uint8 t = 0;
        if (! read (&t, 1)) return false;
        juce::uint8 b[4];
        switch (t)
        {
            case 0x0f: if (! read (b, 1)) return false; f = Field::num (b[0], 1); return true;
            case 0x10: if (! read (b, 2)) return false; f = Field::num (be (b, 2), 2); return true;
            case 0x11: if (! read (b, 4)) return false; f = Field::num (be (b, 4), 4); return true;
            case 0x14:
            {
                if (! read (b, 4)) return false;
                const auto len = be (b, 4);
                if (len > 16 * 1024 * 1024) return false;
                juce::MemoryBlock data ((size_t) len);
                if (len > 0 && ! read (data.getData(), (int) len)) return false;
                f = Field::data (data);
                return true;
            }
            case 0x26:
            {
                if (! read (b, 4)) return false;
                const auto chars = be (b, 4);
                if (chars > 1024 * 1024) return false;
                std::vector<juce::uint8> raw ((size_t) chars * 2);
                if (chars > 0 && ! read (raw.data(), (int) raw.size())) return false;
                juce::String s;
                for (size_t i = 0; i + 1 < raw.size(); i += 2)
                {
                    const auto c = (juce::juce_wchar) ((raw[i] << 8) | raw[i + 1]);
                    if (c == 0) break;
                    s += juce::String::charToString (c);
                }
                f = Field::str (s);
                return true;
            }
            default:
                return false;
        }
    }

    static bool readMessage (const Reader& read, Message& m)
    {
        Field f;
        if (! readField (read, f) || f.number != 0x872349ae) return false;
        if (! readField (read, f)) return false;
        m.txId = f.number;
        if (! readField (read, f)) return false;
        m.type = (int) f.number;
        if (! readField (read, f)) return false;
        const int count = (int) f.number;
        Field tags;
        if (! readField (read, tags) || tags.kind != Field::Kind::Blob) return false;
        m.args.clear();
        juce::uint32 previous = 1;
        for (int i = 0; i < count; ++i)
        {
            const int tag = i < (int) tags.blob.getSize() ? static_cast<const juce::uint8*> (tags.blob.getData())[i] : 0x06;
            if (tag == 0x03 && previous == 0) { m.args.push_back (Field::data ({})); previous = 1; continue; }   // left out
            Field a;
            if (! readField (read, a)) return false;
            m.args.push_back (a);
            previous = a.kind == Field::Kind::Number ? a.number : 1;
        }
        return true;
    }

    int decode (const void* data, size_t size, Message& out)
    {
        size_t pos = 0;
        auto* bytes = static_cast<const juce::uint8*> (data);
        bool shortOfData = false;
        Reader read = [&] (void* dest, int n)
        {
            if (pos + (size_t) n > size) { shortOfData = true; return false; }
            std::memcpy (dest, bytes + pos, (size_t) n);
            pos += (size_t) n;
            return true;
        };
        if (readMessage (read, out)) return (int) pos;
        return shortOfData ? 0 : -1;
    }

    juce::uint32 dmst (int device, int menu, int slot, int trackType)
    {
        return ((juce::uint32) (device & 0xff) << 24) | ((juce::uint32) (menu & 0xff) << 16)
             | ((juce::uint32) (slot & 0xff) << 8) | (juce::uint32) (trackType & 0xff);
    }

    juce::uint32 fourcc (const char* tag)
    {
        juce::uint32 v = 0;
        for (int i = 0; i < 4 && tag[i] != 0; ++i) v |= (juce::uint32) (juce::uint8) tag[i] << (8 * i);
        return v;
    }

    //==========================================================================
    static juce::uint32 argb (int r, int g, int b) { return 0xff000000u | ((juce::uint32) r << 16) | ((juce::uint32) g << 8) | (juce::uint32) b; }

    static juce::uint32 blueToWhite (int whiteness)          // 0 = blue .. 7 = white
    {
        const float k = juce::jlimit (0.0f, 1.0f, (float) whiteness / 7.0f);
        return argb (juce::roundToInt (40 + 210 * k), juce::roundToInt (110 + 140 * k), 255);
    }

    Waveform decodePreview (const juce::MemoryBlock& blob)
    {
        Waveform w;
        auto* p = static_cast<const juce::uint8*> (blob.getData());
        const int columns = juce::jmin (400, (int) blob.getSize() / 2);
        for (int i = 0; i < columns; ++i)
        {
            w.height.push_back ((juce::uint8) (p[i * 2] & 0x1f));
            w.colour.push_back (blueToWhite (p[i * 2 + 1] & 0x07));
        }
        return w;
    }

    Waveform decodeDetail (const juce::MemoryBlock& blob)
    {
        Waveform w;
        w.perSecond = 150;
        auto* p = static_cast<const juce::uint8*> (blob.getData());
        for (size_t i = 0; i < blob.getSize(); ++i)
        {
            w.height.push_back ((juce::uint8) (p[i] & 0x1f));
            w.colour.push_back (blueToWhite (p[i] >> 5));
        }
        return w;
    }

    Waveform decodeColourDetail (const juce::MemoryBlock& blob)
    {
        Waveform w;
        auto* p = static_cast<const juce::uint8*> (blob.getData());
        const int size = (int) blob.getSize();
        int start = -1;
        for (int i = 0; i + 20 <= size && i < 96; ++i)
            if (p[i] == 'P' && p[i + 1] == 'W' && p[i + 2] == 'V' && p[i + 3] == '5') { start = i; break; }
        if (start < 0) return w;
        const int headerLen = (int) be (p + start + 4, 4);
        const int entryBytes = (int) be (p + start + 12, 4);
        const int entries = (int) be (p + start + 16, 4);
        if (entryBytes != 2 || headerLen < 20 || start + headerLen > size) return w;
        w.perSecond = 150;
        const int n = juce::jmin (entries, (size - start - headerLen) / 2);
        for (int i = 0; i < n; ++i)
        {
            const auto v = be (p + start + headerLen + i * 2, 2);
            const int r = (int) (v >> 13) & 7, g = (int) (v >> 10) & 7, b = (int) (v >> 7) & 7;
            w.height.push_back ((juce::uint8) ((v >> 2) & 0x1f));
            w.colour.push_back (argb (r * 255 / 7, g * 255 / 7, b * 255 / 7));
        }
        return w;
    }

    BeatGrid decodeBeatGrid (const juce::MemoryBlock& blob)
    {
        auto* p = static_cast<const juce::uint8*> (blob.getData());
        const int size = (int) blob.getSize();
        auto le = [p] (int at, int bytes) { juce::uint32 v = 0; for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[at + i]; return v; };
        for (int start : { 0x14, 0 })
        {
            BeatGrid g;
            bool ok = size >= start + 32;
            for (int at = start; ok && at + 16 <= size; at += 16)
            {
                const int bib = (int) le (at, 2), t = (int) le (at + 4, 4);
                if (bib < 1 || bib > 4 || (! g.timeMs.empty() && t <= g.timeMs.back())) { ok = g.timeMs.size() > 4; break; }
                g.beatInBar.push_back (bib);
                g.bpm.push_back ((float) le (at + 2, 2) / 100.0f);
                g.timeMs.push_back (t);
            }
            if (ok && g.timeMs.size() > 1) return g;
        }
        return {};
    }

    std::vector<std::pair<int, juce::String>> parseMenuItems (const std::vector<Message>& msgs)
    {
        std::vector<std::pair<int, juce::String>> out;
        for (auto& m : msgs)
            if (m.type == 0x4101 && m.args.size() >= 7)
                out.push_back ({ (int) (m.numberArg (6) & 0xffff), m.stringArg (3) });
        return out;
    }

    TrackInfo trackInfoFrom (const std::vector<Message>& msgs)
    {
        TrackInfo t;
        for (auto& m : msgs)
        {
            if (m.type != 0x4101 || m.args.size() < 7) continue;
            const int type = (int) (m.numberArg (6) & 0xffff);
            const auto label = m.stringArg (3);
            switch (type)
            {
                case 0x04: t.title = label; break;
                case 0x07: t.artist = label; break;
                case 0x02: t.album = label; break;
                case 0x0f: t.key = label; break;
                case 0x06: t.genre = label; break;
                case 0x0b: t.durationSec = (int) m.numberArg (1); break;
                case 0x0d: t.bpm = (float) m.numberArg (1) / 100.0f; break;
                default: break;
            }
        }
        return t;
    }

    //==========================================================================
    bool DbClient::readExact (void* dest, int bytes)
    {
        auto* d = static_cast<char*> (dest);
        int got = 0;
        while (got < bytes)
        {
            if (socket == nullptr || socket->waitUntilReady (true, timeout) != 1) return false;
            const int n = socket->read (d + got, bytes - got, false);
            if (n <= 0) return false;
            got += n;
        }
        return true;
    }

    bool DbClient::readMessage (Message& m)
    {
        Reader read = [this] (void* dest, int n) { return readExact (dest, n); };
        return prodj::readMessage (read, m);
    }

    juce::String DbClient::connect (const juce::String& ip, int asDevice, int timeoutMs)
    {
        timeout = timeoutMs;
        device = asDevice;
        // 1) ask for the database port
        juce::StreamingSocket query;
        if (! query.connect (ip, dbQueryPort, timeoutMs)) return "No database server on " + ip;
        const juce::uint8 ask[] { 0, 0, 0, 0x0f, 'R', 'e', 'm', 'o', 't', 'e', 'D', 'B', 'S', 'e', 'r', 'v', 'e', 'r', 0 };
        query.write (ask, (int) sizeof (ask));
        juce::uint8 portBytes[2] {};
        if (query.waitUntilReady (true, timeoutMs) != 1 || query.read (portBytes, 2, true) != 2) return "Database port query failed on " + ip;
        query.close();
        const int port = (portBytes[0] << 8) | portBytes[1];
        if (port <= 0) return "Player " + ip + " has no database server";

        // 2) greeting
        socket = std::make_unique<juce::StreamingSocket>();
        if (! socket->connect (ip, port, timeoutMs)) { socket.reset(); return "Couldn't connect to " + ip + ":" + juce::String (port); }
        juce::MemoryOutputStream hello;
        writeField (hello, Field::num (1));
        socket->write (hello.getData(), (int) hello.getDataSize());
        juce::uint8 reply[5] {};
        if (! readExact (reply, 5)) { socket.reset(); return "No greeting from " + ip; }

        // 3) setup: we ask as player `asDevice`
        Message setup;
        setup.txId = 0xfffffffe;
        setup.type = 0x0000;
        setup.args = { Field::num ((juce::uint32) asDevice) };
        auto bytes = encode (setup);
        socket->write (bytes.getData(), (int) bytes.getSize());
        Message answer;
        if (! readMessage (answer) || answer.type != 0x4000) { socket.reset(); return "Setup refused by " + ip; }
        return {};
    }

    bool DbClient::request (int type, std::vector<Field> args, Message& reply)
    {
        if (socket == nullptr) return false;
        Message m;
        m.txId = nextTx++;
        m.type = type;
        m.args = std::move (args);
        auto bytes = encode (m);
        if (socket->write (bytes.getData(), (int) bytes.getSize()) != (int) bytes.getSize()) return false;
        return readMessage (reply);
    }

    bool DbClient::renderMenu (int slot, int trackType, int count, std::vector<Message>& items)
    {
        Message m;
        if (! request (0x3000, { Field::num (dmst (device, 1, slot, trackType)), Field::num (0), Field::num ((juce::uint32) count),
                                 Field::num (0), Field::num ((juce::uint32) count), Field::num (0) }, m))
            return false;
        // header 4001, items 4101, footer 4201
        if (m.type != 0x4001) return false;
        for (int guard = 0; guard < count + 4; ++guard)
        {
            Message item;
            if (! readMessage (item)) return false;
            if (item.type == 0x4201) return true;
            items.push_back (item);
        }
        return true;
    }

    Track DbClient::fetchTrack (int slot, int trackType, juce::uint32 id)
    {
        Track t;
        Message reply;
        juce::StringArray errors;

        if (request (0x2002, { Field::num (dmst (device, 1, slot, trackType)), Field::num (id) }, reply) && reply.type == 0x4000)
        {
            const auto count = reply.numberArg (1);
            std::vector<Message> items;
            if (count != 0xffffffffu && count > 0 && count < 64 && renderMenu (slot, trackType, (int) count, items))
                t.info = trackInfoFrom (items);
            else errors.add ("no metadata");
        }
        else errors.add ("metadata refused");

        if (request (0x2204, { Field::num (dmst (device, 8, slot, trackType)), Field::num (id), Field::num (0) }, reply) && reply.blobArg() != nullptr)
            t.grid = decodeBeatGrid (*reply.blobArg());
        if (request (0x2004, { Field::num (dmst (device, 8, slot, trackType)), Field::num (4), Field::num (id), Field::num (0) }, reply)
            && reply.blobArg() != nullptr)
            t.preview = decodePreview (*reply.blobArg());
        if (request (0x2c04, { Field::num (dmst (device, 1, slot, trackType)), Field::num (id), Field::num (fourcc ("PWV5")), Field::num (fourcc ("EXT")) }, reply)
            && reply.blobArg() != nullptr)
            t.detail = decodeColourDetail (*reply.blobArg());
        if (t.detail.empty()
            && request (0x2904, { Field::num (dmst (device, 1, slot, trackType)), Field::num (id), Field::num (0) }, reply)
            && reply.blobArg() != nullptr)
            t.detail = decodeDetail (*reply.blobArg());

        if (t.preview.empty()) errors.add ("no preview");
        if (t.detail.empty()) errors.add ("no waveform");
        if (t.grid.empty()) errors.add ("no beat grid");
        t.error = errors.joinIntoString (", ");
        return t;
    }
} // namespace prodj

//==============================================================================
float ProDjLink::Deck::bpm() const
{
    if (hasPosition && position.bpm > 0.0f) return position.bpm * (1.0f + position.pitch / 100.0f);
    return status.trackBpm > 0.0f ? status.trackBpm * (1.0f + status.pitch / 100.0f) : 0.0f;
}

double ProDjLink::Deck::playheadMs (double now) const
{
    const double rate = 1.0 + (double) status.pitch / 100.0;
    if (hasPosition && now - positionTime < 1.0)
        return position.playheadMs + (status.playing ? (now - positionTime) * 1000.0 * rate : 0.0);
    if (track != nullptr && ! track->grid.empty() && beatAtTime >= 1 && beatAtTime <= (juce::int64) track->grid.timeMs.size())
    {
        const auto& g = track->grid.timeMs;
        const double base = g[(size_t) beatAtTime - 1];
        double ahead = status.playing ? (now - beatTime) * 1000.0 * rate : 0.0;
        if ((size_t) beatAtTime < g.size()) ahead = juce::jmin (ahead, (double) (g[(size_t) beatAtTime] - g[(size_t) beatAtTime - 1]) * 1.05);
        return base + ahead;
    }
    return -1.0;
}

int ProDjLink::Deck::trackLengthMs() const
{
    if (hasPosition && position.trackLengthSec > 0) return position.trackLengthSec * 1000;
    if (track != nullptr && track->info.durationSec > 0) return track->info.durationSec * 1000;
    if (track != nullptr && track->detail.perSecond > 0) return (int) (track->detail.height.size() * 1000 / (size_t) track->detail.perSecond);
    return 0;
}

//==============================================================================
/** Fetches track data from the players' database servers, one job at a time. */
class ProDjLink::Fetcher : private juce::Thread
{
public:
    struct Job { juce::String key, ip; int asDevice = 0, slot = 0, trackType = 0; juce::uint32 id = 0; };

    explicit Fetcher (ProDjLink& o) : juce::Thread ("DJ Link fetcher"), owner (o) { startThread(); }
    ~Fetcher() override { signalThreadShouldExit(); notify(); stopThread (4000); }

    void add (const Job& j)
    {
        {
            const juce::ScopedLock sl (lock);
            for (auto& q : jobs) if (q.key == j.key) return;
            jobs.push_back (j);
        }
        notify();
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Job job;
            {
                const juce::ScopedLock sl (lock);
                if (! jobs.empty()) { job = jobs.front(); jobs.pop_front(); }
            }
            if (job.key.isEmpty()) { wait (500); continue; }

            auto track = std::make_shared<prodj::Track>();
            prodj::DbClient db;
            const auto err = db.connect (job.ip, job.asDevice);
            if (err.isNotEmpty()) track->error = err;
            else *track = db.fetchTrack (job.slot, job.trackType, job.id);
            db.close();

            const juce::ScopedLock sl (owner.lock);
            const bool failed = track->detail.empty() && track->preview.empty() && track->info.title.isEmpty();
            if (! failed) owner.cache[job.key] = track;          // failures are tried again later
            for (auto& [n, d] : owner.deckMap)
            {
                const auto key = juce::String (d.status.trackPlayer) + ":" + juce::String (d.status.slot) + ":" + juce::String ((juce::int64) d.status.rekordboxId);
                if (key == job.key)
                {
                    d.track = failed ? nullptr : track;
                    d.trackState = failed ? track->error + " - trying again..." : juce::String();
                    d.retryAt = failed ? ProDjLink::nowSeconds() + 8.0 : 0.0;
                }
            }
        }
    }

    ProDjLink& owner;
    juce::CriticalSection lock;
    std::deque<Job> jobs;
};

//==============================================================================
ProDjLink::ProDjLink() : juce::Thread ("DJ Link")
{
    fetcher = std::make_unique<Fetcher> (*this);
    startThread();
}

ProDjLink::~ProDjLink()
{
    signalThreadShouldExit();
    stopThread (3000);
    fetcher.reset();
}

void ProDjLink::setSettings (const Settings& s)
{
    {
        const juce::ScopedLock sl (lock);
        settings = s;
    }
    settingsChanged = true;
}

ProDjLink::Settings ProDjLink::getSettings() const
{
    const juce::ScopedLock sl (lock);
    return settings;
}

std::vector<ProDjLink::Deck> ProDjLink::decks() const
{
    const juce::ScopedLock sl (lock);
    std::vector<Deck> out;
    const double now = nowSeconds();
    for (auto& [n, d] : deckMap)
        if (now - d.statusTime < 5.0) out.push_back (d);
    return out;
}

std::vector<prodj::Device> ProDjLink::devices() const
{
    const juce::ScopedLock sl (lock);
    std::vector<prodj::Device> out;
    const double now = nowSeconds();
    for (auto& [k, d] : deviceMap)
        if (now - d.lastSeen < 6.0) out.push_back (d);
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.number < b.number; });
    return out;
}

juce::String ProDjLink::statusText() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

float ProDjLink::masterBpm() const
{
    for (auto& d : decks())
        if (d.status.master) return d.bpm();
    return 0.0f;
}

void ProDjLink::requestTrack (int deckNumber, const prodj::Status& st)
{
    // call with lock held
    auto& d = deckMap[deckNumber];
    d.track.reset();
    d.trackState = {};
    if (st.trackPlayer <= 0 || st.rekordboxId == 0) return;
    if (st.trackType != 1) { d.trackState = "Not a rekordbox track: no waveform"; return; }
    const auto key = juce::String (st.trackPlayer) + ":" + juce::String (st.slot) + ":" + juce::String ((juce::int64) st.rekordboxId);
    if (auto it = cache.find (key); it != cache.end()) { d.track = it->second; return; }

    // the source player's IP, and a player number to ask as (1-4, present, not the source)
    juce::String ip;
    std::vector<int> players;
    for (auto& [k, dev] : deviceMap)
    {
        if (dev.number == st.trackPlayer && ! dev.isMixer) ip = dev.ip;
        if (! dev.isMixer && dev.number >= 1 && dev.number <= 4) players.push_back (dev.number);
    }
    if (ip.isEmpty())
        if (auto it = deckMap.find (st.trackPlayer); it != deckMap.end()) ip = it->second.ip;
    if (ip.isEmpty()) { d.trackState = "Source player " + juce::String (st.trackPlayer) + " not seen yet"; d.retryAt = nowSeconds() + 2.0; return; }

    int asDevice = own.load();
    if (asDevice < 1 || asDevice > 4)
    {
        asDevice = 0;
        for (int p : players)
        {
            if (p == st.trackPlayer) continue;
            bool linked = false;                    // a player that loaded from the source can't be used
            for (auto& [n, other] : deckMap) if (n == p && other.status.trackPlayer == st.trackPlayer) linked = true;
            if (! linked) { asDevice = p; break; }
        }
        if (asDevice == 0) for (int p : players) if (p != st.trackPlayer) { asDevice = p; break; }
        if (asDevice == 0) asDevice = own.load();
    }
    d.trackState = "Loading track data...";
    fetcher->add ({ key, ip, asDevice, st.slot, st.trackType, st.rekordboxId });
}

void ProDjLink::handlePacket (int port, const void* data, int size, const juce::String& ip)
{
    const double now = nowSeconds();
    if (port == prodj::announcePort)
    {
        prodj::Device dev;
        if (! prodj::parseKeepAlive (data, size, dev)) return;
        juce::String ourIp;
        { const juce::ScopedLock sl (lock); ourIp = settings.interfaceIp; }
        if (dev.number == own.load() && (dev.ip == ourIp || dev.name == "Phase")) return;      // ourselves
        dev.lastSeen = now;
        const juce::ScopedLock sl (lock);
        deviceMap[dev.ip + "#" + juce::String (dev.number)] = dev;
        if (dev.number == own.load() && own.load() > 0)
        {
            status = "Player number " + juce::String (own.load()) + " is taken by " + dev.name + " - choosing another";
            own = 0;                                 // someone else has our number: pick again
        }
        return;
    }

    if (port == prodj::statusPort)
    {
        prodj::Status st;
        if (! prodj::parseStatus (data, size, st) || st.number <= 0) return;
        const juce::ScopedLock sl (lock);
        auto& d = deckMap[st.number];
        const bool newTrack = d.statusTime <= 0.0 || st.trackPlayer != d.status.trackPlayer || st.slot != d.status.slot
                           || st.rekordboxId != d.status.rekordboxId;
        const auto previousBeat = d.beatAtTime;
        d.number = st.number;
        d.name = st.name;
        d.ip = ip;
        d.status = st;
        d.statusTime = now;
        // beat counter: beat packets move it on time; status corrects it when it drifts
        if (st.beat >= 0 && (previousBeat < 0 || st.beat > previousBeat || st.beat < previousBeat - 1 || ! st.playing))
        {
            if (st.beat != previousBeat) d.beatTime = now;
            d.beatAtTime = st.beat;
        }
        if (st.beat < 0) d.beatAtTime = -1;
        if (newTrack || (d.track == nullptr && d.retryAt > 0.0 && now > d.retryAt))
        {
            d.retryAt = 0.0;
            requestTrack (st.number, st);
        }
        return;
    }

    if (port == prodj::beatPort)
    {
        prodj::Beat beat;
        prodj::Position pos;
        const juce::ScopedLock sl (lock);
        if (prodj::parseBeat (data, size, beat) && beat.number > 0)
        {
            auto& d = deckMap[beat.number];
            d.beatTime = now;
            if (d.beatAtTime >= 0) ++d.beatAtTime;
            d.status.beatInBar = beat.beatInBar;
            if (d.status.master) ++masterBeats;
        }
        else if (prodj::parsePosition (data, size, pos) && pos.number > 0)
        {
            auto& d = deckMap[pos.number];
            d.hasPosition = true;
            d.position = pos;
            d.positionTime = now;
        }
    }
}

void ProDjLink::chooseNumber()
{
    // with the lock held: a free player number (4, 3, 2, 1 first so metadata requests work, then 5-7)
    Settings s = settings;
    if (s.deviceNumber > 0) { own = s.deviceNumber; return; }
    std::set<int> used;
    for (auto& [k, d] : deviceMap) used.insert (d.number);
    for (int n : { 4, 3, 2, 1, 5, 6, 7 })
        if (used.count (n) == 0) { own = n; return; }
    own = 7;
}

void ProDjLink::sendKeepAlive()
{
    Settings s;
    int peers = 1;
    { const juce::ScopedLock sl (lock); s = settings; peers = (int) deviceMap.size() + 1; }
    if (announceSocket == nullptr || own.load() <= 0) return;
    juce::MACAddress mac;
    for (auto& m : juce::MACAddress::getAllAddresses()) if (! m.isNull()) { mac = m; break; }
    const auto packet = prodj::buildKeepAlive (own.load(), "Phase", mac, s.interfaceIp, peers);
    auto bc = juce::IPAddress::getInterfaceBroadcastAddress (juce::IPAddress (s.interfaceIp));
    announceSocket->write (bc.isNull() ? "255.255.255.255" : bc.toString(), prodj::announcePort, packet.getData(), (int) packet.getSize());
}

void ProDjLink::run()
{
    juce::uint8 buffer[1500];
    while (! threadShouldExit())
    {
        if (settingsChanged.exchange (false))
        {
            announceSocket.reset(); beatSocket.reset(); statusSocket.reset();
            Settings s;
            { const juce::ScopedLock sl (lock); s = settings; }
            if (s.enabled)
            {
                if (s.interfaceIp.isEmpty())
                {
                    const auto ips = localIPv4Addresses();
                    if (! ips.isEmpty()) { const juce::ScopedLock sl (lock); settings.interfaceIp = ips[0]; }
                }
                auto open = [] (int port)
                {
                    auto sock = std::make_unique<juce::DatagramSocket> (true);
                    sock->setEnablePortReuse (true);
                    if (! sock->bindToPort (port)) return std::unique_ptr<juce::DatagramSocket>();
                    return sock;
                };
                announceSocket = open (prodj::announcePort);
                beatSocket = open (prodj::beatPort);
                statusSocket = open (prodj::statusPort);
                startedAt = nowSeconds();
                own = 0;
                const juce::ScopedLock sl (lock);
                status = announceSocket != nullptr && statusSocket != nullptr
                       ? juce::String ("Listening for players...")
                       : juce::String ("Couldn't open the DJ Link ports (50000-50002): is rekordbox or another DJ Link app running here?");
            }
            else
            {
                const juce::ScopedLock sl (lock);
                status = "Off";
            }
        }

        if (announceSocket == nullptr && beatSocket == nullptr && statusSocket == nullptr) { wait (200); continue; }

        bool any = false;
        for (auto [sock, port] : { std::pair<juce::DatagramSocket*, int> { announceSocket.get(), prodj::announcePort },
                                   { beatSocket.get(), prodj::beatPort }, { statusSocket.get(), prodj::statusPort } })
        {
            if (sock == nullptr) continue;
            while (sock->waitUntilReady (true, 0) == 1)
            {
                juce::String ip;
                int senderPort = 0;
                const int n = sock->read (buffer, (int) sizeof (buffer), false, ip, senderPort);
                if (n <= 0) break;
                handlePacket (port, buffer, n, ip);
                any = true;
            }
        }

        const double now = nowSeconds();
        if (own.load() <= 0 && now - startedAt > 2.0)            // listen a moment before taking a number
        {
            const juce::ScopedLock sl (lock);
            chooseNumber();
        }
        if (own.load() > 0 && now - lastKeepAlive > 1.5)
        {
            lastKeepAlive = now;
            sendKeepAlive();
            const juce::ScopedLock sl (lock);
            int players = 0;
            for (auto& [n, d] : deckMap) players += now - d.statusTime < 5.0 ? 1 : 0;
            status = "Online as player " + juce::String (own.load()) + "  -  " + juce::String (players) + " deck(s), "
                   + juce::String ((int) deviceMap.size()) + " device(s) seen";
        }
        if (! any) wait (4);
    }
}

} // namespace pl
