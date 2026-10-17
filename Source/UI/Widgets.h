#pragma once
#include "Theme.h"
#include "Engine/DmxEngine.h"

namespace pl
{

/** UI state shared by the views (not part of the show). Broadcasts when the
    selection, programmer, page or store mode changes. */
struct UiState : public juce::ChangeBroadcaster
{
    int page = 1;
    bool storeArmed = false;
    bool fine = false;                  // Fine: encoders and knobs move 10x finer
    static constexpr float fineFactor = 0.1f;
    int cuePage = 1, cueSlot = 0;       // executor shown in the Cues view (0 = none)
    Family family = Family::Dimmer;     // programmer feature group

    /** Video sources for pixel maps (test patterns + NDI), provided by the main window. */
    std::function<juce::StringArray()> videoSources;
    std::function<juce::String()> ndiStatus;          // "NDI ready - 2 sources" / runtime missing

    /** Copy / Move / Delete keys (MA-style): press, tap the source, then (Copy / Move) the destination. */
    enum class EditMode { None, Copy, Move, Delete };
    EditMode editMode = EditMode::None;
    struct EditItem
    {
        enum class Kind { None, Executor, Preset, Cue } kind = Kind::None;
        int a = 0, b = 0, c = 0;        // executor: page, slot · preset: pool, number · cue: page, slot, index
        juce::String label;
    } editSource;
    void setEditMode (EditMode m) { editMode = m; editSource = {}; if (m != EditMode::None) storeArmed = false; changed(); }

    /** Status line (set by the main window). */
    std::function<void (const juce::String&, bool isError)> message;
    void say (const juce::String& m, bool error = false) { if (message) message (m, error); }

    void changed() { sendChangeMessage(); }
};

namespace ui
{
    /** Small async text prompt. `onDone` gets the trimmed text when confirmed. */
    inline void askForText (juce::Component* parent, const juce::String& title, const juce::String& message,
                            const juce::String& initial, std::function<void (const juce::String&)> onDone)
    {
        auto* aw = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, parent);
        aw->addTextEditor ("text", initial);
        aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, onDone] (int r)
        {
            if (r == 1 && onDone) onDone (aw->getTextEditorContents ("text").trim());
        }), true);
        if (auto* ed = aw->getTextEditor ("text"))
        {
            ed->setFont (theme::font (15.0f));
            ed->grabKeyboardFocus();
            ed->selectAll();
        }
    }
}

/** Rotary encoder-style knob: label, value readout and an optional "Off" knockout. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& labelText, juce::Colour c, bool withKnockout = false)
        : colour (c), hasKnockout (withKnockout)
    {
        label.setText (labelText, juce::dontSendNotification);
        label.setFont (theme::font (12.5f, true));
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        value.setFont (theme::font (13.0f));
        value.setJustificationType (juce::Justification::centred);
        value.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (value);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setScrollWheelEnabled (true);
        slider.onValueChange = [this]
        {
            refreshText();
            if (! updating && onChange) onChange (slider.getValue());
        };
        addAndMakeVisible (slider);
        slider.addMouseListener (this, false);

        if (withKnockout)
        {
            knockout.setButtonText ("Off");
            knockout.setTooltip ("Remove from programmer");
            knockout.onClick = [this] { if (onKnockout) onKnockout(); };
            addAndMakeVisible (knockout);
        }
        setActive (true);
    }

    /** Fine mode: dragging moves 10x less. */
    void setFine (bool f) { slider.setMouseDragSensitivity (f ? 2200 : 220); }

    void setRange (double lo, double hi, double step, double defaultValue)
    {
        slider.setRange (lo, hi, step);
        slider.setDoubleClickReturnValue (true, defaultValue);
    }

    void setValue (double v, bool isActive = true)
    {
        if (slider.isMouseButtonDown()) return;     // don't fight the user's hand
        const juce::ScopedValueSetter<bool> svs (updating, true);
        slider.setValue (v, juce::dontSendNotification);
        setActive (isActive);
        refreshText();
    }

    double getValue() const { return slider.getValue(); }
    float getNormalised() const
    {
        auto r = slider.getRange();
        return r.getLength() > 0.0 ? (float) ((slider.getValue() - r.getStart()) / r.getLength()) : 0.0f;
    }

    /** Turns the knob by `delta` of its full range (MIDI encoders), as if dragged. */
    void nudge (double delta)
    {
        auto range = slider.getRange();
        const double step = slider.getInterval();
        pending += delta * range.getLength();
        if (step > 0.0 && std::abs (pending) < step) return;
        double target = slider.getValue() + (step > 0.0 ? std::trunc (pending / step) * step : pending);
        pending = step > 0.0 ? std::fmod (pending, step) : 0.0;
        slider.setValue (juce::jlimit (range.getStart(), range.getEnd(), target), juce::sendNotificationSync);
    }

    void setKnobColour (juce::Colour c) { colour = c; setActive (active); }

    /** Shows a tick at the default value; the arc is drawn from there. */
    void setDefaultMarker (double defaultValue)
    {
        auto range = slider.getRange();
        slider.getProperties().set ("defaultPos", range.getLength() > 0.0
                                                      ? (defaultValue - range.getStart()) / range.getLength() : 0.0);
        slider.setDoubleClickReturnValue (true, defaultValue);
        slider.repaint();
    }

    void setLabel (const juce::String& t) { label.setText (t, juce::dontSendNotification); }

    /** Replaces the value readout (e.g. "17-83%" for a fanned selection); empty restores it. */
    void setValueText (const juce::String& t) { valueOverride = t; refreshText(); }
    bool isBeingDragged() const { return slider.isMouseButtonDown(); }

    void setActive (bool a)
    {
        active = a;
        slider.setColour (juce::Slider::rotarySliderFillColourId, a ? colour : theme::textDim.withAlpha (0.5f));
        value.setColour (juce::Label::textColourId, a ? colour : theme::textDim);
        repaint();
    }

    std::function<void (double)> onChange;
    std::function<void()> onKnockout;
    std::function<juce::String (double)> formatter;
    /** Right-click: type a value (set when the owner supports it). */
    std::function<void()> onTypeValue;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onTypeValue) onTypeValue();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::panel2.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (active ? colour : theme::line);
        g.fillRoundedRectangle (r.removeFromTop (3.0f).reduced (8.0f, 0.0f), 1.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 5);
        label.setBounds (r.removeFromTop (16));
        if (hasKnockout)
            knockout.setBounds (r.removeFromBottom (20).reduced (10, 0));
        value.setBounds (r.removeFromBottom (16));
        slider.setBounds (r);
    }

private:
    void refreshText()
    {
        const auto v = slider.getValue();
        value.setText (valueOverride.isNotEmpty() ? valueOverride
                       : formatter ? formatter (v) : juce::String (juce::roundToInt (v * 100.0)) + "%",
                       juce::dontSendNotification);
    }

    juce::String valueOverride;
    double pending = 0.0;
    juce::Colour colour;
    bool hasKnockout;
    bool updating = false, active = true;
    juce::Label label, value;
    juce::Slider slider;
    juce::TextButton knockout;
};

/** Vertical fader with a label on top and value underneath (masters). */
class LabelledFader : public juce::Component
{
public:
    LabelledFader (const juce::String& name, juce::Colour c) : colour (c)
    {
        label.setText (name, juce::dontSendNotification);
        label.setFont (theme::font (11.5f, true));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);

        value.setFont (theme::font (13.0f, true));
        value.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (value);

        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange (0.0, 1.0, 0.0);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.onValueChange = [this]
        {
            refreshText();
            if (! updating && onChange) onChange ((float) slider.getValue());
        };
        addAndMakeVisible (slider);
    }

    void setValue (float v)
    {
        if (slider.isMouseButtonDown()) return;
        const juce::ScopedValueSetter<bool> svs (updating, true);
        slider.setValue (v, juce::dontSendNotification);
        refreshText();
    }

    void setDefault (double d) { slider.setDoubleClickReturnValue (true, d); }

    std::function<void (float)> onChange;
    std::function<juce::String (float)> formatter;

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        value.setBounds (r.removeFromTop (18));
        slider.setBounds (r.reduced (0, 4));
    }

private:
    void refreshText()
    {
        const float v = (float) slider.getValue();
        value.setText (formatter ? formatter (v) : juce::String (juce::roundToInt (v * 100.0f)) + "%",
                       juce::dontSendNotification);
    }

    juce::Colour colour;
    bool updating = false;
    juce::Label label, value;
    juce::Slider slider;
};

} // namespace pl
