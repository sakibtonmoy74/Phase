#include <juce_gui_extra/juce_gui_extra.h>
#include "Engine/DmxEngine.h"
#include "Engine/ShowFile.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"

namespace pl
{

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (DmxEngine& engine)
        : DocumentWindow ("Phase", theme::bg, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* content = new MainComponent (engine);
        content->onTitleChanged = [this] (const juce::String& t) { setName (t); };
        setContentOwned (content, false);
        setResizable (true, true);
        setResizeLimits (1100, 700, 10000, 10000);
        centreWithSize (1480, 920);
        setVisible (true);
        content->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class PhaseApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        engine = std::make_unique<DmxEngine>();

        // Restore the last session so a crash or restart never loses the show
        auto autosave = MainComponent::autosaveFile();
        if (autosave.existsAsFile())
            showfile::load (*engine, autosave);

        engine->startEngine();
        mainWindow = std::make_unique<MainWindow> (*engine);
        startAutosave();

        if (getCommandLineParameters().contains ("--midi-selftest"))
            juce::Timer::callAfterDelay (1500, [this]
            {
                if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    mc->runMidiSelfTest();
                quit();
            });
    }

    void shutdown() override
    {
        saveAutosave();
        mainWindow = nullptr;
        engine->stopEngine();
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    void saveAutosave()
    {
        if (engine == nullptr) return;
        auto f = MainComponent::autosaveFile();
        f.getParentDirectory().createDirectory();
        showfile::save (*engine, f);
    }

    void startAutosave()
    {
        autosaveTimer.callback = [this] { saveAutosave(); };
        autosaveTimer.startTimer (60 * 1000);
    }

    struct CallbackTimer : juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { if (callback) callback(); }
    };

    PhaseLookAndFeel lookAndFeel;
    std::unique_ptr<DmxEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    CallbackTimer autosaveTimer;
};

} // namespace pl

START_JUCE_APPLICATION (pl::PhaseApplication)
