#include "MidiControl.h"
#include "Settings.h"

namespace pl
{

juce::String midiTargetName (MidiTarget t, int index)
{
    const auto n = juce::String (index);
    switch (t)
    {
        case MidiTarget::ExecFader: return "Exec " + n + " fader";
        case MidiTarget::ExecGo:    return "Exec " + n + " button 1 (Go)";
        case MidiTarget::ExecBack:  return "Exec " + n + " button 2 (Back)";
        case MidiTarget::ExecOff:   return "Exec " + n + " button 3 (Off)";
        case MidiTarget::ExecFlash: return "Exec " + n + " button 4 (Flash)";
        case MidiTarget::Encoder:   return "Encoder " + n;
        case MidiTarget::Grand:     return "Grand master";
        case MidiTarget::DimSpeed:  return "Dimmer FX speed";
        case MidiTarget::MoveSpeed: return "Movement FX speed";
        case MidiTarget::Size:      return "Movement FX size";
        case MidiTarget::Blackout:  return "Blackout";
        case MidiTarget::Tap:       return "Tap tempo";
        case MidiTarget::PageUp:    return "Page +";
        case MidiTarget::PageDown:  return "Page -";
        case MidiTarget::Store:     return "Store";
        case MidiTarget::Clear:     return "Clear";
        case MidiTarget::Fan:       return "Fan";
        case MidiTarget::EffectKnob: return "Effects knob " + n;
        case MidiTarget::Next:      return "Next";
        case MidiTarget::Prev:      return "Prev";
        case MidiTarget::StepAll:   return "All (after Next / Prev)";
        case MidiTarget::Highlight: return "Highlight";
        case MidiTarget::Lowlight:  return "Lowlight";
        case MidiTarget::KnobLayer: return "Knob layer: " + MidiControl::knobLayerName (index);
        case MidiTarget::Fine:      return "Fine (encoders 10x finer)";
    }
    return {};
}

bool midiTargetIsContinuous (MidiTarget t)
{
    return t == MidiTarget::ExecFader || t == MidiTarget::Encoder || t == MidiTarget::EffectKnob || t == MidiTarget::Grand
        || t == MidiTarget::DimSpeed || t == MidiTarget::MoveSpeed || t == MidiTarget::Size;
}

//==============================================================================
bool MidiBinding::matches (const juce::MidiMessage& m) const
{
    if (m.getChannel() != channel) return false;
    switch (kind)
    {
        case Kind::CC:        return m.isController() && m.getControllerNumber() == number;
        case Kind::Note:      return (m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() == number;
        case Kind::PitchBend: return m.isPitchWheel();
    }
    return false;
}

bool MidiBinding::matchesTouch (const juce::MidiMessage& m) const
{
    if (touchNumber < 0 || m.getChannel() != (touchChannel > 0 ? touchChannel : channel)) return false;
    return touchIsNote ? (m.isNoteOnOrOff() && m.getNoteNumber() == touchNumber)
                       : (m.isController() && m.getControllerNumber() == touchNumber);
}

juce::String MidiBinding::describe() const
{
    juce::String s;
    switch (kind)
    {
        case Kind::CC:        s = "CC " + juce::String (number); break;
        case Kind::Note:      s = "Note " + juce::String (number); break;
        case Kind::PitchBend: s = "Pitch bend"; break;
    }
    s << "  ch" << channel;
    if (touchNumber >= 0) s << "  (touch " << (touchIsNote ? "note " : "CC ") << touchNumber << ")";
    return s;
}

juce::var MidiBinding::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("target", (int) target);
    o->setProperty ("index", index);
    o->setProperty ("kind", (int) kind);
    o->setProperty ("channel", channel);
    o->setProperty ("number", number);
    o->setProperty ("encoderMode", (int) encoderMode);
    if (feedbackNumber >= 0) o->setProperty ("feedbackNumber", feedbackNumber);
    if (feedbackOn != 127) o->setProperty ("feedbackOn", feedbackOn);
    if (touchNumber >= 0) { o->setProperty ("touchNumber", touchNumber); o->setProperty ("touchIsNote", touchIsNote); }
    if (touchChannel > 0) o->setProperty ("touchChannel", touchChannel);
    if (feedback != Feedback::Value) o->setProperty ("feedback", (int) feedback);
    if (layer >= 0) o->setProperty ("layer", layer);
    return juce::var (o);
}

MidiBinding MidiBinding::fromVar (const juce::var& v)
{
    MidiBinding b;
    b.target = (MidiTarget) juce::jlimit (0, (int) MidiTarget::Fine, (int) v["target"]);
    b.index = (int) v["index"];
    b.kind = (Kind) juce::jlimit (0, 2, (int) v["kind"]);
    b.channel = juce::jlimit (1, 16, (int) v.getProperty ("channel", 1));
    b.number = juce::jlimit (0, 127, (int) v["number"]);
    b.encoderMode = (EncoderMode) juce::jlimit (0, 4, (int) v["encoderMode"]);
    b.feedbackNumber = juce::jlimit (-1, 127, (int) v.getProperty ("feedbackNumber", -1));
    b.feedbackOn = juce::jlimit (1, 127, (int) v.getProperty ("feedbackOn", 127));
    b.touchNumber = (int) v.getProperty ("touchNumber", -1);
    b.touchIsNote = (bool) v.getProperty ("touchIsNote", false);
    b.touchChannel = juce::jlimit (0, 16, (int) v.getProperty ("touchChannel", 0));
    b.feedback = (Feedback) juce::jlimit (0, 2, (int) v.getProperty ("feedback", 0));
    b.layer = (int) v.getProperty ("layer", -1);
    if (b.feedbackOn != 127 && b.feedback == Feedback::Value && b.kind == Kind::Note) b.feedback = Feedback::Led;   // v0.7 maps
    return b;
}

//==============================================================================
MidiControl::MidiControl (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    load();
    openInputs();
    if (outputId.isNotEmpty()) setOutput (outputId);
    startTimerHz (100);
}

MidiControl::~MidiControl()
{
    stopTimer();
    for (auto& d : openDevices) d->stop();
}

void MidiControl::openInputs()
{
    for (auto& d : openDevices) d->stop();
    openDevices.clear();
    for (auto& info : juce::MidiInput::getAvailableDevices())
        if (enabledInputs.contains (info.identifier))
            if (auto dev = juce::MidiInput::openDevice (info.identifier, this))
            {
                dev->start();
                openDevices.push_back (std::move (dev));
            }
}

void MidiControl::setInputEnabled (const juce::String& id, bool enabled)
{
    if (enabled) enabledInputs.addIfNotAlreadyThere (id);
    else         enabledInputs.removeString (id);
    openInputs();
    save();
    sendChangeMessage();
}

void MidiControl::setOutput (const juce::String& id)
{
    output.reset();
    outputId = id;
    if (id.isNotEmpty())
        output = juce::MidiOutput::openDevice (id);
    lastSent.clear();                 // resend everything to the new device
    save();
    sendChangeMessage();
}

//==============================================================================
void MidiControl::startLearn (MidiTarget t, int index)
{
    touchCandidate = {};
    learning = true;
    learnTarget = t;
    learnIndex = index;
    sendChangeMessage();
}

void MidiControl::cancelLearn()
{
    learning = false;
    sendChangeMessage();
}

const MidiBinding* MidiControl::bindingFor (MidiTarget t, int index) const
{
    for (auto& b : bindings)
        if (b.target == t && b.index == index) return &b;
    return nullptr;
}

void MidiControl::clearBinding (MidiTarget t, int index)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                                    [t, index] (const MidiBinding& b) { return b.target == t && b.index == index; }),
                    bindings.end());
    save();
    sendChangeMessage();
}

void MidiControl::setEncoderMode (int encoder, MidiBinding::EncoderMode mode)
{
    setEncoderMode (MidiTarget::Encoder, encoder, mode);
}

void MidiControl::setEncoderMode (MidiTarget t, int index, MidiBinding::EncoderMode mode)
{
    for (auto& b : bindings)
        if (b.target == t && b.index == index) b.encoderMode = mode;
    lastAbsolute.clear();
    save();
    sendChangeMessage();
}

void MidiControl::clearAllBindings()
{
    bindings.clear();
    lastSent.clear();
    save();
    sendChangeMessage();
}

juce::StringArray MidiControl::presetNames()
{
    return { "Behringer X-Touch Compact (Mackie Control)", "Behringer X-Touch Compact (Standard MIDI)" };
}

juce::File MidiControl::mapsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Phase").getChildFile ("MIDI Maps");
}

juce::Array<juce::File> MidiControl::savedMaps()
{
    auto files = mapsFolder().findChildFiles (juce::File::findFiles, false, "*.phasemidi");
    files.sort();
    return files;
}

juce::String MidiControl::saveMap (const juce::File& f) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "phase-midi-map");
    o->setProperty ("version", 1);
    o->setProperty ("name", f.getFileNameWithoutExtension());
    juce::Array<juce::var> arr;
    for (auto& b : bindings) arr.add (b.toVar());
    o->setProperty ("bindings", arr);
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (juce::JSON::toString (juce::var (o))))
        return "Couldn't write " + f.getFullPathName();
    return {};
}

juce::String MidiControl::loadMap (const juce::File& f)
{
    const auto v = juce::JSON::parse (f.loadFileAsString());
    auto* arr = v["bindings"].getArray();
    if (v["format"].toString() != "phase-midi-map" || arr == nullptr) return "Not a Phase MIDI map: " + f.getFileName();
    std::vector<MidiBinding> b;
    for (auto& x : *arr) b.push_back (MidiBinding::fromVar (x));
    touched.clear();
    knobLayer = 0;
    lastAbsolute.clear();
    setBindings (b);
    return {};
}

juce::String MidiControl::knobLayerName (int layer)
{
    switch (layer)
    {
        case 0:  return "Programmer";
        case 1:  return "Effects 1-8";
        case 2:  return "Effects 9-16";
        default: return "Layer " + juce::String (layer + 1);
    }
}

void MidiControl::setKnobLayer (int layer)
{
    if (layer == knobLayer) return;
    knobLayer = layer;
    lastSent.clear();                  // rings and layer LEDs show the new layer
    lastAbsolute.clear();
    lastMessage = "Knobs: " + knobLayerName (layer);
    sendChangeMessage();
}

juce::String MidiControl::loadPreset (const juce::String& name)
{
    const bool mackie = name.containsIgnoreCase ("Mackie");
    if (! name.startsWith ("Behringer X-Touch Compact")) return "Unknown preset";

    std::vector<MidiBinding> b;
    using K = MidiBinding::Kind;
    using F = MidiBinding::Feedback;
    auto add = [&b] (MidiTarget t, int index, K kind, int number, int channel = 1) -> MidiBinding&
    {
        MidiBinding x;
        x.target = t; x.index = index; x.kind = kind; x.channel = channel; x.number = number;
        b.push_back (x);
        return b.back();
    };
    auto led = [] (MidiBinding& x, int feedbackNumber, int on) { x.feedback = F::Led; x.feedbackNumber = feedbackNumber; x.feedbackOn = on; };

    if (mackie)
    {
        // X-Touch Compact in MC mode (hold MC while powering on, or loading this preset asks it to switch).
        // Faders: 14-bit pitch bend on channels 1-9, touch = notes 104-112. V-Pots: CC 16-23
        // (sign + ticks), rings CC 48-55. Buttons: notes, LEDs on the same note (127 on).
        const auto vpot = MidiBinding::EncoderMode::RelativeSigned;
        for (int i = 1; i <= 8; ++i)
        {
            auto& f = add (MidiTarget::ExecFader, i, K::PitchBend, 0, i);          // faders 1-8: executors 1-8 of the page
            f.touchNumber = 103 + i; f.touchIsNote = true; f.touchChannel = 1;
            led (add (MidiTarget::ExecGo,    i, K::Note, i - 1),  -1, 127);         // REC row    -> executor button 1
            led (add (MidiTarget::ExecBack,  i, K::Note, 7 + i),  -1, 127);         // SOLO row   -> button 2
            led (add (MidiTarget::ExecOff,   i, K::Note, 15 + i), -1, 127);         // MUTE row   -> button 3
            led (add (MidiTarget::ExecFlash, i, K::Note, 23 + i), -1, 127);         // SELECT row (under the fader) -> button 4
        }
        auto& g = add (MidiTarget::Grand, 0, K::PitchBend, 0, 9);                   // main fader
        g.touchNumber = 112; g.touchIsNote = true; g.touchChannel = 1;

        // the 8 top knobs, in layers: Programmer / Effects 1-8 / Effects 9-16
        auto knob = [&] (MidiTarget t, int index, int k, int layer)
        {
            auto& x = add (t, index, K::CC, 15 + k);
            x.encoderMode = vpot; x.layer = layer;
            x.feedback = F::MackieRing; x.feedbackNumber = 47 + k;
        };
        for (int k = 1; k <= 5; ++k) knob (MidiTarget::Encoder, k, k, 0);
        knob (MidiTarget::DimSpeed, 0, 6, 0);
        knob (MidiTarget::MoveSpeed, 0, 7, 0);
        knob (MidiTarget::Size, 0, 8, 0);
        for (int k = 1; k <= 8; ++k) knob (MidiTarget::EffectKnob, k, k, 1);
        for (int k = 1; k <= 8; ++k) knob (MidiTarget::EffectKnob, 8 + k, k, 2);

        // pushing a top knob toggles Fine
        for (int k = 1; k <= 8; ++k) led (add (MidiTarget::Fine, 0, K::Note, 31 + k), -1, 127);

        // right-hand encoder pushes (V-Pot assign): knob layers, highlight, lowlight, all
        led (add (MidiTarget::KnobLayer, 0, K::Note, 40), -1, 127);
        led (add (MidiTarget::KnobLayer, 1, K::Note, 41), -1, 127);
        led (add (MidiTarget::KnobLayer, 2, K::Note, 42), -1, 127);
        led (add (MidiTarget::Highlight, 0, K::Note, 43), -1, 127);
        led (add (MidiTarget::Lowlight,  0, K::Note, 44), -1, 127);
        add (MidiTarget::StepAll, 0, K::Note, 45);
        // the two stepping encoders: Bank = page, Channel = Prev / Next
        add (MidiTarget::PageDown, 0, K::Note, 46);
        add (MidiTarget::PageUp,   0, K::Note, 47);
        add (MidiTarget::Prev,     0, K::Note, 48);
        add (MidiTarget::Next,     0, K::Note, 49);
        // the button under the main fader (Flip) and the right-hand buttons
        led (add (MidiTarget::Blackout, 0, K::Note, 50), -1, 127);
        add (MidiTarget::Tap,       0, K::Note, 84);                                 // Marker
        add (MidiTarget::Fan,       0, K::Note, 85);                                 // Nudge
        led (add (MidiTarget::Highlight, 0, K::Note, 86), -1, 127);                  // Cycle
        add (MidiTarget::Prev,      0, K::Note, 91);                                 // Rewind
        add (MidiTarget::Next,      0, K::Note, 92);                                 // Fast forward
        add (MidiTarget::Clear,     0, K::Note, 93);                                 // Stop
        led (add (MidiTarget::Store, 0, K::Note, 94), -1, 127);                      // Play
        led (add (MidiTarget::Lowlight, 0, K::Note, 95), -1, 127);                   // Record
    }
    else
    {
        // X-Touch Compact, Standard MIDI mode, Layer A (factory map, channel 1).
        // Feedback (motor faders, button LEDs, encoder rings) goes to the global channel (1).
        auto ring = [] (MidiBinding& x, int fb) { x.feedbackNumber = fb; x.encoderMode = MidiBinding::EncoderMode::AbsoluteEndless; };
        for (int i = 1; i <= 8; ++i)
        {
            add (MidiTarget::ExecFader, i, K::CC, i).touchNumber = 100 + i;           // faders 1-8, touch CC 101-108
            led (add (MidiTarget::ExecGo,    i, K::Note, 15 + i), i - 1, 1);         // button row 1 -> executor button 1
            led (add (MidiTarget::ExecBack,  i, K::Note, 23 + i), 7 + i, 1);         // row 2 -> button 2
            led (add (MidiTarget::ExecOff,   i, K::Note, 31 + i), 15 + i, 1);        // row 3 -> button 3
            led (add (MidiTarget::ExecFlash, i, K::Note, 39 + i), 23 + i, 1);        // button under the fader -> button 4
            ring (add (MidiTarget::EffectKnob, i, K::CC, 17 + i), 33 + i);           // right-hand encoders -> Effects knobs
        }
        add (MidiTarget::Grand, 0, K::CC, 9).touchNumber = 109;                       // main fader
        for (int k = 0; k < 8; ++k) add (MidiTarget::Fine, 0, K::Note, k);           // push a top encoder: Fine
        for (int k = 8; k < 16; ++k) add (MidiTarget::Fine, 0, K::Note, k);          // push a right-hand encoder: Fine
        for (int i = 1; i <= 5; ++i)
            ring (add (MidiTarget::Encoder, i, K::CC, 9 + i), 25 + i);               // top encoders 1-5 -> programmer
        ring (add (MidiTarget::DimSpeed,  0, K::CC, 15), 31);                         // top encoders 6-8 -> masters
        ring (add (MidiTarget::MoveSpeed, 0, K::CC, 16), 32);
        ring (add (MidiTarget::Size,      0, K::CC, 17), 33);
        led (add (MidiTarget::Blackout, 0, K::Note, 48), 32, 1);                      // button under the main fader
        led (add (MidiTarget::PageDown, 0, K::Note, 49), 33, 1);                      // right-hand buttons
        led (add (MidiTarget::PageUp,   0, K::Note, 50), 34, 1);
        led (add (MidiTarget::Tap,      0, K::Note, 51), 35, 1);
        led (add (MidiTarget::Store,    0, K::Note, 52), 36, 1);
        led (add (MidiTarget::Clear,    0, K::Note, 53), 37, 1);
        led (add (MidiTarget::Fan,      0, K::Note, 54), 38, 1);
    }

    bindings = b;
    lastSent.clear();
    lastSentTime.clear();
    lastAbsolute.clear();
    touched.clear();
    touchSeen.clear();
    knobLayer = 0;

    // use the controller's ports if it's connected
    juce::String found;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("X-TOUCH COMPACT")) { enabledInputs.addIfNotAlreadyThere (d.identifier); found = d.name; }
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("X-TOUCH COMPACT")) { outputId = d.identifier; output = juce::MidiOutput::openDevice (d.identifier); break; }
    if (output != nullptr && found.isNotEmpty())
    {
        output->sendMessageNow (juce::MidiMessage::controllerEvent (1, 127, mackie ? 1 : 0));   // operation mode: MC / Standard
        if (! mackie) output->sendMessageNow (juce::MidiMessage::programChange (1, 0));      // Layer A
    }
    openInputs();
    save();
    sendChangeMessage();
    const juce::String what = mackie ? "X-Touch Compact (Mackie Control)" : "X-Touch Compact (Standard MIDI)";
    return found.isNotEmpty() ? what + " mapped (" + found + ")"
                              : what + " mapped - connect it, then tick its input and pick it as feedback output";
}

//==============================================================================
void MidiControl::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const juce::ScopedLock sl (queueLock);
    if (queue.size() < 4096) queue.push_back (m);
}

void MidiControl::timerCallback()
{
    std::vector<juce::MidiMessage> pending;
    {
        const juce::ScopedLock sl (queueLock);
        pending.swap (queue);
    }

    // collapse runs of the same continuous control to its latest value
    for (size_t i = 0; i < pending.size(); ++i)
    {
        auto& m = pending[i];
        if (i + 1 < pending.size())
        {
            auto& next = pending[i + 1];
            const bool sameCC = m.isController() && next.isController() && m.getChannel() == next.getChannel()
                                && m.getControllerNumber() == next.getControllerNumber();
            const bool samePB = m.isPitchWheel() && next.isPitchWheel() && m.getChannel() == next.getChannel();
            if ((sameCC || samePB) && ! learning)
            {
                // keep relative encoder ticks, drop superseded absolute values
                bool relative = false;
                for (auto& b : bindings)
                    if ((b.encoderMode == MidiBinding::EncoderMode::Relative2s || b.encoderMode == MidiBinding::EncoderMode::Relative64)
                        && b.matches (m))
                        relative = true;
                if (! relative) continue;
            }
        }
        handleMessage (m);
    }

    if (++tick % 3 == 0)
        sendFeedback();
}

void MidiControl::handleMessage (const juce::MidiMessage& m)
{
    if (! (m.isController() || m.isNoteOnOrOff() || m.isPitchWheel()))
        return;

    lastMessage = m.getDescription();
    {
        juce::String line;
        if (m.isController())      line << "CC " << m.getControllerNumber() << " = " << m.getControllerValue();
        else if (m.isNoteOn())     line << "Note " << m.getNoteNumber() << " on " << (int) m.getVelocity();
        else if (m.isNoteOff())    line << "Note " << m.getNoteNumber() << " off";
        else                       line << "Pitch bend " << m.getPitchWheelValue();
        line << "   ch " << m.getChannel();
        if (recent.size() > 0 && recent[0].startsWith (line.upToFirstOccurrenceOf (" = ", false, false) + " = ") && m.isController())
            recent.set (0, line);                   // a moving control: update its line
        else if (recent.size() > 0 && m.isPitchWheel() && recent[0].startsWith ("Pitch bend") && recent[0].endsWith ("ch " + juce::String (m.getChannel())))
            recent.set (0, line);
        else
            recent.insert (0, line);
        while (recent.size() > 14) recent.remove (recent.size() - 1);
    }

    if (learning)
    {
        learnMessage (m);
        return;
    }

    // touch-sensitive faders: finger on / off
    {
        bool wasTouch = false;
        for (auto& b : bindings)
            if (active (b) && b.matchesTouch (m))
            {
                wasTouch = true;
                const auto key = keyOf (b.target, b.index);
                const bool on = m.isController() ? m.getControllerValue() >= 64 : (m.isNoteOn() && m.getVelocity() > 0);
                touchSeen.insert (key);
                if (on) touched.insert (key);
                else
                {
                    touched.erase (key);
                    lastSent.erase (key);          // let go: the motor goes to the real value (page change, limits)
                    lastSentTime.erase (key);
                }
            }
        if (wasTouch) return;
    }

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    for (auto& b : bindings)
    {
        if (! active (b) || ! b.matches (m)) continue;
        const auto key = keyOf (b.target, b.index);

        float value = 0.0f;
        int raw = 0;
        bool pressed = false;
        if (m.isPitchWheel())       { raw = m.getPitchWheelValue(); value = (float) raw / 16383.0f; pressed = raw > 8191; }
        else if (m.isController())  { raw = m.getControllerValue(); value = (float) raw / 127.0f; pressed = raw >= 64; }
        else                        { raw = m.isNoteOn() ? m.getVelocity() : 0; value = raw > 0 ? 1.0f : 0.0f; pressed = raw > 0; }

        // relative / endless controls give a change instead of a position
        bool relative = false;
        float delta = 0.0f;
        const auto mode = b.encoderMode;
        if (m.isController() && (mode != MidiBinding::EncoderMode::Absolute || b.target == MidiTarget::EffectKnob))
        {
            relative = true;
            int steps = 0;
            if (mode == MidiBinding::EncoderMode::Relative2s)      steps = raw < 64 ? raw : raw - 128;
            else if (mode == MidiBinding::EncoderMode::Relative64) steps = raw - 64;
            else if (mode == MidiBinding::EncoderMode::RelativeSigned) steps = (raw & 0x40) != 0 ? -(raw & 0x3f) : (raw & 0x3f);
            else
            {
                // absolute knob used as endless: the difference, and keep going at the ends
                auto it = lastAbsolute.find (key);
                if (it != lastAbsolute.end())
                {
                    steps = raw - it->second;
                    if (steps == 0) steps = raw >= 127 ? 1 : raw <= 0 ? -1 : 0;
                }
                lastAbsolute[key] = raw;
            }
            delta = (float) steps / (mode == MidiBinding::EncoderMode::Relative2s || mode == MidiBinding::EncoderMode::Relative64 ? 200.0f : 127.0f);
            // Fine: everything but the programmer encoders (the programmer applies it itself)
            if (ui.fine && b.target != MidiTarget::Encoder) delta *= UiState::fineFactor;
        }

        // motor faders: ignore what the motor reports while it moves to our feedback.
        // With touch sensing a finger on the fader always wins.
        if (midiTargetIsContinuous (b.target) && b.target != MidiTarget::Encoder && ! relative)
        {
            // a fader whose touch sensor works only counts while a finger is on it:
            // whatever it sends untouched is the motor following us
            if (touched.count (key) == 0 && touchSeen.count (key) > 0) continue;
            if (touched.count (key) == 0)
            {
                // without a finger on it, a touch-sensing fader moving right after we drove it is the motor
                auto t = lastSentTime.find (key);
                if (t != lastSentTime.end() && now - t->second < (b.touchNumber >= 0 ? 1.0 : 0.25)) continue;
            }
            lastSent[key] = raw;      // the surface is already here: don't echo it back
        }

        auto master = [&] (std::atomic<float>& target)
        {
            target = juce::jlimit (0.0f, 1.0f, relative ? target.load() + delta : value);
        };

        const int page = ui.page, slot = b.index;
        auto& pb = engine.playback;
        switch (b.target)
        {
            case MidiTarget::ExecFader:
            {
                const juce::ScopedLock sl (engine.dataLock);
                pb.setFader (page, slot, relative ? pb.faderPosition (page, slot) + delta : value, DmxEngine::now());
                break;
            }
            // executor buttons by position: they do whatever the executor's button is set to
            case MidiTarget::ExecGo:
            case MidiTarget::ExecBack:
            case MidiTarget::ExecOff:
            case MidiTarget::ExecFlash:
            {
                const int button = b.target == MidiTarget::ExecGo ? 0 : b.target == MidiTarget::ExecBack ? 1
                                 : b.target == MidiTarget::ExecOff ? 2 : 3;
                // Store armed, or Copy / Move / Delete: an executor button picks the executor
                if ((ui.storeArmed || ui.editMode != UiState::EditMode::None) && onExecutorTap)
                {
                    if (pressed) onExecutorTap (slot);
                    break;
                }
                const juce::ScopedLock sl (engine.dataLock);
                engine.executorButton (page, slot, button, pressed);
                break;
            }

            case MidiTarget::Encoder:
                if (onEncoder)
                {
                    if (relative) { if (std::abs (delta) > 0.0f) onEncoder (b.index, delta, false, 0.0f); }
                    else          onEncoder (b.index, 0.0f, true, value);
                }
                break;

            case MidiTarget::EffectKnob:
                if (onEffectKnob && std::abs (delta) > 0.0f) onEffectKnob (b.index, delta);
                break;

            case MidiTarget::Grand:     master (engine.masters.grand); break;
            case MidiTarget::DimSpeed:  master (engine.masters.dimmerSpeed); break;
            case MidiTarget::MoveSpeed: master (engine.masters.movementSpeed); break;
            case MidiTarget::Size:      master (engine.masters.size); break;

            case MidiTarget::Blackout: case MidiTarget::Tap: case MidiTarget::PageUp: case MidiTarget::PageDown:
            case MidiTarget::Store: case MidiTarget::Clear: case MidiTarget::Fan:
            case MidiTarget::Next: case MidiTarget::Prev: case MidiTarget::StepAll:
            case MidiTarget::Highlight: case MidiTarget::Lowlight: case MidiTarget::Fine:
                if (pressed && onKey) onKey (b.target);
                break;

            case MidiTarget::KnobLayer:
                if (pressed) setKnobLayer (b.index);
                break;
        }
    }
}

//==============================================================================
bool MidiControl::learnMessage (const juce::MidiMessage& m)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool continuous = midiTargetIsContinuous (learnTarget);

    MidiBinding b;
    b.target = learnTarget;
    b.index = learnIndex;
    b.channel = m.getChannel();
    if (m.isPitchWheel())      b.kind = MidiBinding::Kind::PitchBend;
    else if (m.isController()) { b.kind = MidiBinding::Kind::CC; b.number = m.getControllerNumber(); }
    else                       { b.kind = MidiBinding::Kind::Note; b.number = m.getNoteNumber(); }

    if (continuous && ! m.isPitchWheel())
    {
        // Touch-sensitive faders send a touch message (CC 0/127 or a note) before they
        // move: remember it as a touch candidate and wait for the real movement.
        const bool onOff = m.isNoteOnOrOff() || m.getControllerValue() == 0 || m.getControllerValue() == 127;
        const bool repeat = touchCandidate.number == b.number && touchCandidate.kind == b.kind
                            && touchCandidate.channel == b.channel;
        if (onOff && ! repeat)
        {
            if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0) || (m.isController() && m.getControllerValue() == 0))
                return false;                 // a release: nothing to learn
            touchCandidate = { b.kind, b.channel, b.number, now };
            return false;
        }
    }
    if (continuous && touchCandidate.number >= 0 && now - touchCandidate.time < 3.0
        && ! (touchCandidate.number == b.number && touchCandidate.kind == b.kind && touchCandidate.channel == b.channel)
        && (m.isPitchWheel() || touchCandidate.channel == b.channel))
    {
        b.touchNumber = touchCandidate.number;
        b.touchIsNote = touchCandidate.kind == MidiBinding::Kind::Note;
        if (touchCandidate.channel != b.channel) b.touchChannel = touchCandidate.channel;
    }
    else if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0))
        return false;                         // buttons learn on press

    // a control that is layered (Mackie knobs) is learned into the current layer
    auto sameControl = [&b] (const MidiBinding& x)
    {
        return x.kind == b.kind && x.channel == b.channel && (x.kind == MidiBinding::Kind::PitchBend || x.number == b.number);
    };
    for (auto& x : bindings)
        if (sameControl (x) && x.layer >= 0) { b.layer = knobLayer; b.encoderMode = x.encoderMode; b.feedback = x.feedback; b.feedbackNumber = x.feedbackNumber; break; }

    // one control per target, and one target per control (per layer)
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(), [&] (const MidiBinding& x)
    {
        return (x.target == b.target && x.index == b.index) || (sameControl (x) && x.layer == b.layer);
    }), bindings.end());
    bindings.push_back (b);
    learning = false;
    touchCandidate = {};
    save();
    sendChangeMessage();
    return true;
}

//==============================================================================
void MidiControl::sendFeedback()
{
    if (output == nullptr) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    for (auto& b : bindings)
    {
        if (! active (b)) continue;
        float v = -1.0f;
        {
            const juce::ScopedLock sl (engine.dataLock);
            auto& st = engine.playback.state (ui.page, b.index);
            auto* exec = engine.playback.find (ui.page, b.index);
            const auto t = b.target;
            // executor button LEDs follow what the button does
            // executor button LEDs: occupied executors light up (so you see them when changing pages),
            // running ones light every button; held Flash / Swop / Temp / Pause light theirs.
            // While Store / Copy / Move / Delete waits for a target, they show where you can press:
            // blinking = available (empty slots for Store, used ones to pick from, empty slots to copy to)
            const bool occupied = exec != nullptr && ! exec->cues.empty();
            auto buttonLed = [&] (int button) -> float
            {
                using M = UiState::EditMode;
                if (ui.storeArmed) return occupied ? 1.0f : blinkValue;
                if (ui.editMode != M::None)
                {
                    const auto& src = ui.editSource;
                    const bool isSource = src.kind == UiState::EditItem::Kind::Executor && src.a == ui.page && src.b == b.index;
                    if (ui.editMode == M::Delete) return occupied ? blinkValue : 0.0f;
                    if (src.kind == UiState::EditItem::Kind::None) return occupied ? blinkValue : 0.0f;   // pick the source
                    if (isSource) return 1.0f;
                    return occupied ? 1.0f : blinkValue;                                              // where it can go
                }
                if (exec == nullptr) return 0.0f;
                switch (exec->buttons[button])
                {
                    case ButtonFunction::Flash: if (st.flash)  return 1.0f; break;
                    case ButtonFunction::Swop:  if (st.swop)   return 1.0f; break;
                    case ButtonFunction::Temp:  if (st.temp)   return 1.0f; break;
                    case ButtonFunction::Pause: if (st.paused) return blinkValue; break;
                    case ButtonFunction::Go: case ButtonFunction::Toggle: case ButtonFunction::Back: case ButtonFunction::Off:
                    case ButtonFunction::None: case ButtonFunction::Select: case ButtonFunction::RateUp: case ButtonFunction::RateDown:
                        break;
                }
                if (! occupied) return 0.0f;
                return (button == 0 || button == 3 || st.isOn()) ? 1.0f : 0.0f;
            };
            const bool rowLed = b.feedbackNumber >= 0 || b.feedback == MidiBinding::Feedback::Led;
            if      (t == MidiTarget::ExecFader) v = engine.playback.faderPosition (ui.page, b.index);
            else if (t == MidiTarget::ExecGo)    v = buttonLed (0);
            else if (t == MidiTarget::ExecBack)  v = rowLed ? buttonLed (1) : -1.0f;
            else if (t == MidiTarget::ExecOff)   v = rowLed ? buttonLed (2) : -1.0f;
            else if (t == MidiTarget::ExecFlash) v = buttonLed (3);
            else if (t == MidiTarget::Grand)     v = engine.masters.grand.load();
            else if (t == MidiTarget::DimSpeed)  v = engine.masters.dimmerSpeed.load();
            else if (t == MidiTarget::MoveSpeed) v = engine.masters.movementSpeed.load();
            else if (t == MidiTarget::Size)      v = engine.masters.size.load();
            else if (t == MidiTarget::Blackout)  v = engine.masters.blackout.load() ? 1.0f : 0.0f;
            else if (t == MidiTarget::Store)     v = ui.storeArmed ? 1.0f : 0.0f;
            else if (t == MidiTarget::KnobLayer) v = knobLayer == b.index ? 1.0f : 0.0f;
        }
        if ((b.target == MidiTarget::Highlight || b.target == MidiTarget::Lowlight) && keyState)
            v = keyState (b.target) ? 1.0f : 0.0f;
        if (b.target == MidiTarget::Fine && b.feedback == MidiBinding::Feedback::Led) v = ui.fine ? 1.0f : 0.0f;
        // encoder rings (only when the binding names a feedback control)
        if (b.feedbackNumber >= 0)
        {
            if (b.target == MidiTarget::Encoder && encoderValue)      v = encoderValue (b.index);
            if (b.target == MidiTarget::EffectKnob && effectKnobValue) v = effectKnobValue (b.index);
            if (v < 0.0f && (b.target == MidiTarget::Encoder || b.target == MidiTarget::EffectKnob)) v = 0.0f;   // ring off
        }
        if (v < 0.0f) continue;

        const auto key = keyOf (b.target, b.index);
        if (touched.count (key) > 0) continue;          // finger on the fader: don't fight it
        juce::MidiMessage msg;
        int raw = 0;
        const int number = b.feedbackNumber >= 0 ? b.feedbackNumber : b.number;
        switch (b.kind)
        {
            case MidiBinding::Kind::PitchBend: raw = juce::roundToInt (v * 16383.0f); msg = juce::MidiMessage::pitchWheel (b.channel, raw); break;
            case MidiBinding::Kind::CC:
                if (b.feedback == MidiBinding::Feedback::MackieRing)
                    raw = 0x20 | (1 + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 10.0f));   // wrap mode, LEDs 1-11
                else
                    raw = juce::roundToInt (v * 127.0f);
                msg = juce::MidiMessage::controllerEvent (b.channel, number, raw);
                break;
            case MidiBinding::Kind::Note:
                // LEDs: on velocity or 0 (X-Touch standard: 1 on; Mackie: 127 on)
                if (b.feedback == MidiBinding::Feedback::Led || b.feedbackOn != 127)
                    raw = std::abs (v - blinkValue) < 0.001f ? (b.feedbackOn == 127 ? 1 : 2)     // blink: Mackie 1, X-Touch standard 2
                        : (v >= 0.5f ? b.feedbackOn : 0);
                else
                    raw = juce::roundToInt (v * 127.0f);
                msg = juce::MidiMessage::noteOn (b.channel, number, (juce::uint8) raw);
                break;
        }
        auto it = lastSent.find (key);
        if (it != lastSent.end() && it->second == raw) continue;
        output->sendMessageNow (msg);
        lastSent[key] = raw;
        if (midiTargetIsContinuous (b.target)) lastSentTime[key] = now;
    }
}

//==============================================================================
void MidiControl::save()
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> ins;
    for (auto& id : enabledInputs) ins.add (id);
    o->setProperty ("inputs", ins);
    o->setProperty ("output", outputId);
    juce::Array<juce::var> b;
    for (auto& x : bindings) b.add (x.toVar());
    o->setProperty ("bindings", b);
    settings::set ("midi", juce::var (o));
}

void MidiControl::load()
{
    auto v = settings::get ("midi");
    if (auto* ins = v["inputs"].getArray())
        for (auto& id : *ins) enabledInputs.add (id.toString());
    outputId = v["output"].toString();
    if (auto* b = v["bindings"].getArray())
        for (auto& x : *b) bindings.push_back (MidiBinding::fromVar (x));
}

} // namespace pl
