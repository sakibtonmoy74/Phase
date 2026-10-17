cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect on macOS
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Phase VERSION 0.8.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local ./JUCE checkout if present, otherwise download it.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Engine sources: no UI dependencies, shared by the app and the tests.
# ---------------------------------------------------------------------------
set(ENGINE_SOURCES
    Source/Engine/Attributes.cpp
    Source/Engine/FixtureType.cpp
    Source/Engine/Patch.cpp
    Source/Engine/Effects.cpp
    Source/Engine/Playback.cpp
    Source/Engine/Presets.cpp
    Source/Engine/AudioReactive.cpp
    Source/Engine/AudioAnalysis.cpp
    Source/Engine/PixelMap.cpp
    Source/Engine/ArtNet.cpp
    Source/Engine/DmxEngine.cpp
    Source/Engine/ShowFile.cpp
    Source/Engine/Gdtf.cpp
    Source/Engine/UndoHistory.cpp
    Source/Engine/FixtureTemplates.cpp
    Source/Engine/CommandLine.cpp
    Source/Engine/Stage3D.cpp)

set(UI_SOURCES
    Source/Main.cpp
    Source/UI/MainComponent.cpp
    Source/UI/LayoutView.cpp
    Source/UI/CuesView.cpp
    Source/UI/PresetsView.cpp
    Source/UI/PatchView.cpp
    Source/UI/FixtureBuilderView.cpp
    Source/UI/EffectsPanel.cpp
    Source/UI/ProgrammerBar.cpp
    Source/UI/ExecutorBar.cpp
    Source/UI/DmxMonitorView.cpp
    Source/UI/NetworkView.cpp
    Source/UI/MidiView.cpp
    Source/UI/AudioView.cpp
    Source/UI/ScreenWindow.cpp
    Source/Control/MidiControl.cpp
    Source/Control/AudioInput.cpp
    Source/Control/NdiInput.cpp
    Source/Control/GdtfShare.cpp
    Source/Control/ProDjLink.cpp
    Source/UI/GdtfShareView.cpp
    Source/UI/Visualizer3DView.cpp
    Source/UI/DjLinkView.cpp)

set(COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
option(PHASE_BUILD_APP "Build the Phase app (OFF = engine tests only)" ON)

if(PHASE_BUILD_APP)
juce_add_gui_app(Phase
    PRODUCT_NAME "Phase"
    COMPANY_NAME "Phase AV"
    BUNDLE_ID    com.phaseav.phase
    VERSION      ${PROJECT_VERSION}
    ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Phase uses audio inputs to read LTC timecode."
    PLIST_TO_MERGE [[
        <plist><dict>
            <key>NSLocalNetworkUsageDescription</key>
            <string>Phase sends Art-Net DMX to lighting nodes on your network.</string>
        </dict></plist>
    ]])

target_sources(Phase PRIVATE ${ENGINE_SOURCES} ${UI_SOURCES})

juce_add_binary_data(PhaseResources SOURCES Resources/AppIcon.png Resources/LogoMark.png)
target_include_directories(Phase PRIVATE Source)
target_compile_definitions(Phase PRIVATE ${COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="Phase"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Phase
    PRIVATE
        PhaseResources
        juce::juce_gui_extra
        juce::juce_audio_devices
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Engine tests (headless): ctest or ./PhaseTests
# ---------------------------------------------------------------------------
juce_add_console_app(PhaseTests PRODUCT_NAME "PhaseTests")
target_sources(PhaseTests PRIVATE ${ENGINE_SOURCES} Source/Control/GdtfShare.cpp Source/Control/ProDjLink.cpp Tests/EngineTests.cpp)
target_include_directories(PhaseTests PRIVATE Source)
target_compile_definitions(PhaseTests PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(PhaseTests
    PRIVATE juce::juce_core juce::juce_events
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME EngineTests COMMAND PhaseTests)
