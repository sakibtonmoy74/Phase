#pragma once
#include "Widgets.h"
#include "Engine/Stage3D.h"

namespace pl
{

/** 3D stage view: fixtures at their 3D positions with live beams (colour,
    intensity, pan / tilt, zoom) and floor pools, so looks and effects can be
    previewed. Drag to orbit, right-drag to pan, scroll to zoom. "Place" lets
    you click, drag and type fixture positions and mounts. */
class Visualizer3DView : public juce::Component,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    Visualizer3DView (DmxEngine&, UiState&);
    ~Visualizer3DView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { if (placeButton.getToggleState()) repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;

private:
    struct Fixture3
    {
        PatchedFixture f;
        const FixtureType* type = nullptr;
        V3 pos;
        bool hasPanTilt = false, hasZoom = false;
    };

    struct Camera
    {
        V3 target { 0.0f, 1.5f, 0.0f };
        float yaw = 0.0f, pitch = 22.0f, distance = 16.0f, fov = 50.0f;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    void frameAll();

    // camera maths
    V3 cameraPosition() const;
    void basis (V3& right, V3& up, V3& forward) const;
    bool project (V3 p, juce::Point<float>& out, float* depth = nullptr) const;
    V3 rayDirection (juce::Point<float> screen) const;
    juce::Rectangle<float> viewArea() const;

    int fixtureAt (juce::Point<float>) const;
    std::vector<int> selectedFixtures() const;
    void applyToSelection (std::function<void (PatchedFixture&)>);
    void refreshPanel();

    DmxEngine& engine;
    UiState& ui;
    std::vector<Fixture3> fixtures;
    Camera cam;
    bool framed = false;

    // toolbar
    juce::TextButton frameButton { "Frame" }, frontButton { "Front" }, topButton { "Top" }, sideButton { "Side" },
                     placeButton { "Place" }, hazeButton { "Haze" },
                     selectButton { "Select" }, allButton { "All" }, noneButton { "None" }, groupsButton { "Groups" };
    void groupsMenu();
    void boxSelect (juce::Rectangle<float>, bool add, bool rightToLeft, bool bottomToTop);
    juce::Slider beamSlider;
    juce::Label hint;

    // placement panel
    class PlacePanel;
    std::unique_ptr<PlacePanel> panel;

    enum class Drag { None, Orbit, Pan, Move, MoveY, Axis, Turn, Box } drag = Drag::None;

    // handles (Place mode): arrows move along X / Y / Z, the ring turns the selection
    bool gizmoCentre (V3& centre) const;
    float gizmoLength() const { return juce::jmax (0.4f, cam.distance * 0.14f); }
    int gizmoHit (juce::Point<float>) const;          // 0 none, 1 X, 2 Y, 3 Z, 4 ring
    void paintGizmo (juce::Graphics&);
    int dragAxis = 0;
    V3 dragCentre;
    std::map<int, float> turnStart;

    /** Align / distribute / arrange tools for the selection (Place panel). */
    void arrange3D (const juce::String& how);
    juce::Point<float> last, downPos;
    std::map<int, V3> moveStart;
    V3 dragPlanePoint;
    float dragPlaneY = 0.0f;
    bool clickAdd = false;
    int clickedFixture = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Visualizer3DView)
};

} // namespace pl
