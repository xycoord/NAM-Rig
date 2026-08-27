// Custom standalone shell (replaces JUCE's default StandaloneFilterApp via
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP).
//
// Differences from the JUCE default, all deliberate:
//  * Native window title bar and border — the JUCE-drawn ones read as
//    non-native, especially on Linux.
//  * Audio input UNMUTED by default. This is a guitar rig; booting up muted
//    fails "plug in and play". A saved user choice is still respected.
//  * No Options button in the title bar (native bars have no room for one);
//    the editor shows a settings button instead when running standalone.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace namrig
{

class MainWindow final : public juce::StandaloneFilterWindow
{
public:
    MainWindow(const juce::String& title, juce::Colour bg,
               std::unique_ptr<juce::StandalonePluginHolder> holder)
        : StandaloneFilterWindow(title, bg, std::move(holder))
    {
        // Full native decoration set (the base class asks for min+close only).
        setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
        setUsingNativeTitleBar(true);

        // The base class puts an "Options" TextButton in its drawn title bar;
        // with a native bar it would float over the content. Hide it.
        for (int i = 0; i < getNumChildComponents(); ++i)
            if (auto* b = dynamic_cast<juce::TextButton*>(getChildComponent(i)))
                if (b->getButtonText() == "Options")
                    b->setVisible(false);
    }
};

class StandaloneApp final : public juce::JUCEApplication
{
public:
    StandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#else
        options.folderName = "";
#endif
        appProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override
    {
#if JUCE_LINUX
        // Ask PipeWire for a low-latency quantum before any audio client
        // opens. setenv with overwrite=0: an explicit user env still wins.
        // Milestone 4 exposes this as a latency preference in our own UI.
        setenv("PIPEWIRE_LATENCY", "64/48000", 0);
        setenv("PIPEWIRE_QUANTUM", "64/48000", 0);
#endif

        // takeOwnershipOfSettings MUST be false: the PropertiesFile belongs
        // to appProperties. The parameter defaults to true, which makes the
        // holder delete it on window close and the ApplicationProperties
        // destructor delete it again — shutdown segfault, lost settings.
        auto holder = std::make_unique<juce::StandalonePluginHolder>(
            appProperties.getUserSettings(), /*takeOwnershipOfSettings*/ false);

        // Default to JACK (via pipewire-jack) when the user has no saved
        // audio setup: direct graph scheduling beats the ALSA bridge for
        // latency. No-op if no JACK library is present, and a saved device
        // choice is never overridden.
        if (auto* settings = appProperties.getUserSettings())
        {
            if (settings->getValue("audioSetup").isEmpty())
                holder->deviceManager.setCurrentAudioDeviceType("JACK", true);
        }

        // Default the input to live. Only applies when the user has never
        // touched the setting; their saved choice wins otherwise.
        if (auto* settings = appProperties.getUserSettings())
            if (!settings->containsKey("shouldMuteInput"))
                holder->getMuteInputValue().setValue(false);

        mainWindow = std::make_unique<MainWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            std::move(holder));
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace namrig

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new namrig::StandaloneApp();
}
