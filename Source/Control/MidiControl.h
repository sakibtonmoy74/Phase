#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <set>
#include "UI/Widgets.h"

namespace pl
{

/** Everything a MIDI control can be mapped to. Executor targets follow the
    current page; encoders follow the attributes shown in the programmer. */
enum class MidiTarget
{
    ExecFader, ExecGo, ExecBack, ExecFlash, ExecOff,
    Encoder,
    Grand, DimSpeed, MoveSpeed, Size,
    Blackout, Tap, PageUp, PageDown, Store, Clear, Fan,
    EffectKnob,                     // the knobs shown in the Effects panel, in order
    Next, Prev, StepAll, Highlight, Lowlight,
    KnobLayer,                      // picks what the layered knobs do (index = layer)
    Fine                            // toggles fine mode (encoder pushes)
};

juce::String midiTargetName (MidiTarget, int index);
bool midiTargetIsContinuous (MidiTarget);

struct MidiBinding
{
    enum class Kind { CC, Note, PitchBend };
    /** Absolute: knob position. Relative: endless encoders (two's complement or
        64 = no change). AbsoluteEndless: an absolute knob used like an endless
        one (changes are applied as steps; X-Touch style, keeps going at the ends). */
    enum class EncoderMode { Absolute, Relative2s, Relative64, AbsoluteEndless, RelativeSigned };
    /** How feedback is sent: the value, an LED (feedbackOn velocity / 0), or a
        Mackie Control V-Pot ring (CC 48-55: mode | position 1-11). */
    enum class Feedback { Value, Led, MackieRing };

    MidiTarget target = MidiTarget::ExecFader;
    int index = 0;                  // executor slot 1-10 or encoder 1-4
    Kind kind = Kind::CC;
    int channel = 1;                // 1-16
    int number = 0;                 // CC or note number (unused for pitch bend)
    EncoderMode encoderMode = EncoderMode::Absolute;
    int feedbackNumber = -1;        // LED / ring feedback on another CC or note (-1 = same as input)
    int feedbackOn = 127;           // note velocity for "LED on" (X-Touch: 1)
    /** Touch-sensitive motor faders: the CC (or note) the fader sends while a
        finger is on it (X-Touch Compact standard mode: CC 101-109; Mackie: notes
        104-112). While touched the motor isn't driven and every move counts. */
    int touchNumber = -1;
    bool touchIsNote = false;
    int touchChannel = 0;           // 0 = the control's channel (Mackie: touch notes on 1, faders on 1-9)
    Feedback feedback = Feedback::Value;
    int layer = -1;                 // knob layer this binding belongs to (-1 = always active)

    bool matchesTouch (const juce::MidiMessage&) const;

    bool matches (const juce::MidiMessage&) const;
    juce::String describe() const;
    juce::var toVar() const;
    static MidiBinding fromVar (const juce::var&);
};

/** MIDI input, learn, mapping and feedback (motor faders, button LEDs).
    Incoming messages are queued from the MIDI thread and handled on the
    message thread. Mappings live in the machine settings, not the show. */
class MidiControl : private juce::MidiInputCallback,
                    private juce::Timer,
                    public juce::ChangeBroadcaster
{
public:
    MidiControl (DmxEngine&, UiState&);
    ~MidiControl() override;

    // --- devices --------------------------------------------------------------
    juce::Array<juce::MidiDeviceInfo> inputs() const  { return juce::MidiInput::getAvailableDevices(); }
    juce::Array<juce::MidiDeviceInfo> outputs() const { return juce::MidiOutput::getAvailableDevices(); }
    bool isInputEnabled (const juce::String& id) const { return enabledInputs.contains (id); }
    void setInputEnabled (const juce::String& id, bool enabled);
    juce::String getOutputId() const { return outputId; }
    void setOutput (const juce::String& id);

    // --- mapping --------------------------------------------------------------
    void startLearn (MidiTarget, int index);
    void cancelLearn();
    bool isLearning() const { return learning; }
    bool isLearning (MidiTarget t, int index) const { return learning && learnTarget == t && learnIndex == index; }
    const MidiBinding* bindingFor (MidiTarget, int index) const;
    void clearBinding (MidiTarget, int index);
    void setEncoderMode (int encoder, MidiBinding::EncoderMode);
    void setEncoderMode (MidiTarget, int index, MidiBinding::EncoderMode);

    /** Ready-made maps for known controllers. */
    static juce::StringArray presetNames();
    /** Replaces all mappings with a preset and selects the controller's ports when found. */
    juce::String loadPreset (const juce::String& name);
    void clearAllBindings();

    // --- saved maps (.phasemidi files: all the mappings, encoder modes and feedback) --
    static juce::File mapsFolder();                 // ~/Library/Application Support/Phase/MIDI Maps
    static juce::Array<juce::File> savedMaps();
    juce::String saveMap (const juce::File&) const;
    juce::String loadMap (const juce::File&);
    std::vector<MidiBinding> getBindings() const { return bindings; }
    void setBindings (const std::vector<MidiBinding>& b) { bindings = b; lastSent.clear(); save(); sendChangeMessage(); }
    juce::String getLastMessage() const { return lastMessage; }
    /** The last incoming messages, newest first (MIDI monitor). */
    juce::StringArray getRecentMessages() const { return recent; }

    /** Knob layer (Mackie preset: push a right-hand encoder to switch what the top knobs do). */
    int getKnobLayer() const { return knobLayer; }
    void setKnobLayer (int layer);
    static juce::String knobLayerName (int layer);

    /** A touch-sensitive fader currently has a finger on it. */
    bool isTouched (MidiTarget t, int index) const { return touched.count (keyOf (t, index)) > 0; }

    /** For tests and for the MIDI page: handle a message as if it arrived. */
    void handleMessage (const juce::MidiMessage&);

    // --- hooks into the UI ----------------------------------------------------
    /** Encoder 1-4 turned: relative delta (in 0..1 units) or an absolute value. */
    std::function<void (int encoder, float delta, bool absolute, float value)> onEncoder;
    /** Store / Clear / Fan / Tap / Blackout / page keys, Next / Prev / All, Highlight / Lowlight. */
    std::function<void (MidiTarget)> onKey;
    /** An executor button pressed while Store / Copy / Move / Delete waits for a target. */
    std::function<void (int slot)> onExecutorTap;
    /** For key LEDs: is Highlight / Lowlight on. */
    std::function<bool (MidiTarget)> keyState;
    /** Effects panel knob n turned by delta (in 0..1 of its range). */
    std::function<void (int knob, float delta)> onEffectKnob;
    /** For encoder ring feedback: current value 0..1 of encoder / effects knob n, or < 0. */
    std::function<float (int encoder)> encoderValue;
    std::function<float (int knob)> effectKnobValue;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void sendFeedback();
    void openInputs();
    void save();
    void load();

    using Key = std::pair<int, int>;   // target, index
    static Key keyOf (MidiTarget t, int index) { return { (int) t, index }; }

    DmxEngine& engine;
    UiState& ui;

    juce::StringArray enabledInputs;
    std::vector<std::unique_ptr<juce::MidiInput>> openDevices;
    juce::String outputId;
    std::unique_ptr<juce::MidiOutput> output;

    juce::CriticalSection queueLock;
    std::vector<juce::MidiMessage> queue;

    std::vector<MidiBinding> bindings;
    bool learning = false;
    MidiTarget learnTarget = MidiTarget::ExecFader;
    int learnIndex = 0;

    std::set<Key> touched, touchSeen;
    static constexpr float blinkValue = 0.75f;     // feedback value meaning "LED blinking"
    int knobLayer = 0;
    bool active (const MidiBinding& b) const { return b.layer < 0 || b.layer == knobLayer; }
    struct TouchCandidate { MidiBinding::Kind kind; int channel = 0, number = -1; double time = 0.0; } touchCandidate;
    bool learnMessage (const juce::MidiMessage&);

    std::map<Key, int> lastSent;
    std::map<Key, int> lastAbsolute;        // AbsoluteEndless: previous raw value
    std::map<Key, double> lastSentTime;
    juce::String lastMessage;
    juce::StringArray recent;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiControl)
};

} // namespace pl
