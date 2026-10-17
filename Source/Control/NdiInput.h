#pragma once
#include <juce_core/juce_core.h>
#include "Engine/PixelMap.h"

namespace pl
{

/** NDI video input for pixel mapping. Loads the NDI runtime at run time (from
    NDI Tools / NDI Runtime), discovers sources and receives the ones pixel maps
    use at low bandwidth, writing small frames into the VideoHub as
    "NDI: <source name>". Without the runtime it simply reports unavailable. */
class NdiInput : private juce::Thread
{
public:
    explicit NdiInput (VideoHub&);
    ~NdiInput() override;

    bool isAvailable() const      { return available.load(); }
    juce::String getStatus() const;

    /** Names of NDI sources on the network (as "NDI: <name>"). */
    juce::StringArray getSources() const;

    /** Sources to receive (as "NDI: <name>"); others are disconnected. */
    void setWanted (const juce::StringArray&);

private:
    void run() override;

    VideoHub& hub;
    std::atomic<bool> available { false };

    mutable juce::CriticalSection lock;
    juce::String status { "Looking for the NDI runtime..." };
    juce::StringArray discovered, wanted;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NdiInput)
};

} // namespace pl
