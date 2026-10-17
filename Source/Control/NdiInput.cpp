#include "NdiInput.h"

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
#include "../../ThirdParty/NDI/Processing.NDI.Lib.h"
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace pl
{

static constexpr int kMaxFrameWidth = 192;   // pixel mapping needs very little resolution

NdiInput::NdiInput (VideoHub& h) : juce::Thread ("NDI input"), hub (h)
{
    startThread (juce::Thread::Priority::low);
}

NdiInput::~NdiInput() { stopThread (3000); }

juce::String NdiInput::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

juce::StringArray NdiInput::getSources() const
{
    const juce::ScopedLock sl (lock);
    return discovered;
}

void NdiInput::setWanted (const juce::StringArray& names)
{
    const juce::ScopedLock sl (lock);
    wanted = names;
}

//==============================================================================
static std::unique_ptr<juce::DynamicLibrary> loadRuntime (juce::StringArray& tried)
{
    juce::StringArray candidates;
    for (auto* env : { NDILIB_REDIST_FOLDER, "NDI_RUNTIME_DIR_V5" })
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable (env, {});
        if (dir.isNotEmpty())
        {
            candidates.add (juce::File (dir).getChildFile (NDILIB_LIBRARY_NAME).getFullPathName());
            candidates.add (juce::File (dir).getChildFile ("libndi.dylib").getFullPathName());
        }
    }
   #if JUCE_MAC
    // NDI Tools / NDI Runtime installers, Homebrew, the SDKs
    for (auto* p : { "/usr/local/lib/libndi.dylib", "/usr/local/lib/libndi.6.dylib", "/usr/local/lib/libndi.5.dylib",
                     "/opt/homebrew/lib/libndi.dylib",
                     "/Library/NDI SDK for Apple/lib/macOS/libndi.dylib",
                     "/Library/NDI SDK for macOS/lib/macOS/libndi.dylib",
                     "/Library/NDI Advanced SDK for Apple/lib/macOS/libndi_advanced.dylib",
                     "/Library/Application Support/NDI/lib/libndi.dylib" })
        candidates.add (p);
    // NDI apps carry the runtime inside them (NDI Video Monitor, Scan Converter, Studio Monitor...)
    for (auto& app : juce::File ("/Applications").findChildFiles (juce::File::findDirectories, false, "*NDI*.app"))
        for (auto& lib : app.getChildFile ("Contents/Frameworks").findChildFiles (juce::File::findFiles, true, "libndi*.dylib"))
            candidates.add (lib.getFullPathName());
    for (auto& folder : juce::File ("/Applications").findChildFiles (juce::File::findDirectories, false, "NDI*"))
        for (auto& lib : folder.findChildFiles (juce::File::findFiles, true, "libndi*.dylib"))
            candidates.add (lib.getFullPathName());
   #elif JUCE_LINUX
    candidates.add ("libndi.so.6");
    candidates.add ("libndi.so.5");
   #endif
    candidates.add (NDILIB_LIBRARY_NAME);

    for (auto& path : candidates)
    {
        tried.add (path);
        auto lib = std::make_unique<juce::DynamicLibrary>();
        if (lib->open (path) && (lib->getFunction ("NDIlib_v6_load") != nullptr || lib->getFunction ("NDIlib_v5_load") != nullptr))
            return lib;
    }
    return nullptr;
}

void NdiInput::run()
{
    // look for the runtime now and every 10 seconds (NDI Tools may be installed while Phase runs)
    std::unique_ptr<juce::DynamicLibrary> lib;
    const NDIlib_v6* ndi = nullptr;
    while (! threadShouldExit())
    {
        juce::StringArray tried;
        lib = loadRuntime (tried);
        using LoadFn = const NDIlib_v6* (*) (void);
        LoadFn load = nullptr;
        if (lib != nullptr)
        {
            load = (LoadFn) lib->getFunction ("NDIlib_v6_load");
            if (load == nullptr) load = (LoadFn) lib->getFunction ("NDIlib_v5_load");
        }
        ndi = load != nullptr ? load() : nullptr;
        if (ndi != nullptr && ndi->initialize()) break;
        ndi = nullptr;
        {
            const juce::ScopedLock sl (lock);
            status = "NDI runtime not found - install NDI Tools (ndi.video/tools), Phase finds it by itself";
        }
        wait (10000);
    }
    if (ndi == nullptr) return;
    available = true;
    { const juce::ScopedLock sl (lock); status = "NDI ready"; }

    NDIlib_find_create_t findSettings;
    findSettings.show_local_sources = true;
    auto finder = ndi->find_create_v2 (&findSettings);

    std::map<juce::String, NDIlib_recv_instance_t> receivers;   // keyed "NDI: name"
    auto lastScan = juce::Time::getMillisecondCounter() - 10000;

    while (! threadShouldExit())
    {
        // refresh the source list once a second
        if (juce::Time::getMillisecondCounter() - lastScan > 1000 && finder != nullptr)
        {
            lastScan = juce::Time::getMillisecondCounter();
            uint32_t count = 0;
            auto* srcs = ndi->find_get_current_sources (finder, &count);
            juce::StringArray names;
            for (uint32_t i = 0; i < count; ++i)
                if (srcs[i].p_ndi_name != nullptr)
                    names.add ("NDI: " + juce::String::fromUTF8 (srcs[i].p_ndi_name));

            juce::StringArray want;
            {
                const juce::ScopedLock sl (lock);
                discovered = names;
                want = wanted;
                status = "NDI ready - " + juce::String (names.size()) + " source(s), receiving " + juce::String ((int) receivers.size());
            }

            // connect / disconnect receivers to match the pixel maps
            for (auto it = receivers.begin(); it != receivers.end();)
            {
                if (! want.contains (it->first))
                {
                    ndi->recv_destroy (it->second);
                    hub.remove (it->first);
                    it = receivers.erase (it);
                }
                else ++it;
            }
            for (auto& w : want)
                if (! receivers.count (w) && names.contains (w))
                {
                    const auto ndiName = w.fromFirstOccurrenceOf ("NDI: ", false, false);
                    NDIlib_source_t src;
                    src.p_ndi_name = ndiName.toRawUTF8();
                    NDIlib_recv_create_v3_t rc (src, NDIlib_recv_color_format_BGRX_BGRA, NDIlib_recv_bandwidth_lowest, false, "Phase pixel map");
                    if (auto r = ndi->recv_create_v3 (&rc))
                        receivers[w] = r;
                }
        }

        // pull the latest video from each receiver
        bool gotAny = false;
        for (auto& [name, recv] : receivers)
        {
            NDIlib_video_frame_v2_t vf;
            if (ndi->recv_capture_v2 (recv, &vf, nullptr, nullptr, 0) != NDIlib_frame_type_video)
                continue;
            gotAny = true;
            if (vf.p_data != nullptr && vf.xres > 0 && vf.yres > 0
                && (vf.FourCC == NDIlib_FourCC_video_type_BGRA || vf.FourCC == NDIlib_FourCC_video_type_BGRX))
            {
                auto frame = std::make_shared<VideoFrame>();
                frame->width = juce::jmin (kMaxFrameWidth, vf.xres);
                frame->height = juce::jmax (1, vf.yres * frame->width / vf.xres);
                frame->pixels.resize ((size_t) (frame->width * frame->height));
                const int stride = vf.line_stride_in_bytes > 0 ? vf.line_stride_in_bytes : vf.xres * 4;
                for (int y = 0; y < frame->height; ++y)
                {
                    const auto* row = vf.p_data + (size_t) (y * vf.yres / frame->height) * (size_t) stride;
                    for (int x = 0; x < frame->width; ++x)
                    {
                        const auto* p = row + (size_t) (x * vf.xres / frame->width) * 4;   // B, G, R, A
                        frame->pixels[(size_t) (y * frame->width + x)] =
                            0xff000000u | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[1] << 8) | (juce::uint32) p[0];
                    }
                }
                hub.setFrame (name, frame);
            }
            ndi->recv_free_video_v2 (recv, &vf);
        }
        wait (gotAny ? 5 : 15);
    }

    for (auto& [name, recv] : receivers) { ndi->recv_destroy (recv); hub.remove (name); }
    if (finder != nullptr) ndi->find_destroy (finder);
    ndi->destroy();
}

} // namespace pl
