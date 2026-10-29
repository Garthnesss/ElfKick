/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <simulator/kick_Simulator.h>
// ElfKick: reuse the combined app's device adapter (header-only) to arm
// the hardware-input path in the simulator — see initialise() below.
#include "../kick/kick_device_adapter.h"
//==============================================================================
class KickApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    KickApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise (const juce::String& /*commandLine*/) override
    {
        // This method is where you should put your application's initialisation code..

        mainWindow.reset (new MainWindow (getApplicationName()));

        // ElfKick: arm the hardware-input path, mirroring apps/kick (which
        // holds a KickDeviceAdapter for exactly this). The adapter's ctor
        // calls Controls::InputEvents::initialize() — connecting the
        // SIM_BUILD wrapper in input_events.cpp (SimulatorInterface::
        // hwInputReceived -> dispatchHardwareEvent) — and bridges
        // dispatchHardwareEvent -> contracts::DclSignals::
        // hardwareInputTriggered, which the embedded KickHome listens to.
        // Upstream kickem never did either, so panel input emitted into a
        // signal with zero slots (see PAD_TEST_LOG.md).
        //
        // ORDER MATTERS: the adapter is created AFTER MainWindow.
        // sim::SimulatorIntrefaceImpl is a singleton whose operator new
        // returns the existing instance and whose constructor then re-runs
        // on it, re-creating its hwInputReceived signal and dropping any
        // connections already made (the "somehow clearing the connected
        // signals" effect noted in input_events.cpp). The panel — and the
        // last impl constructions — happens inside MainWindow, so after
        // this point initialize()'s factory instance IS the panel's own
        // and its connection survives.
        deviceAdapter.reset (new KickDeviceAdapter());
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        deviceAdapter = nullptr;
        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& /*commandLine*/) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setFullScreen(true);
            setUsingNativeTitleBar (true);
            setContentOwned (&siml, true);
            setResizable (false, false);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */

    private:
        sim::Simulator siml;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    // ElfKick: hardware-input bridge (see initialise()).
    std::unique_ptr<KickDeviceAdapter> deviceAdapter;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (KickApplication)
