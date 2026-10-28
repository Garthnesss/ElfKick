#pragma once

#include <kick_ui/kick_ui.h>
#include <contracts/dcl/dcl_contracts.h>
#include "kick_SimulatorInterfaceImpl.h"
#include "kick_SimulatorMainControl.h"
#include "kick_SimulatorControls.h"
namespace sim
{
// ElfKick: simulator hardware-event id space (consumed by the SIM_BUILD path
// in modules/input_events/input_events.cpp, which wraps these into
// CPktMgr::HWMsg_t). Buttons use contracts::DeviceControlContracts::
// BtnLedSequence values directly; pads and qlink knobs get their own ranges
// because BtnLedSequence has no entries for them.
constexpr int SIM_PAD_ID_BASE  = 1000; // + PadSequence (0..15)
constexpr int SIM_KNOB_ID_BASE = 1100; // + qlink index (0..3)

using PadButtonState = contracts::DeviceControlContracts::PadButtonState;
using PadColourIds   = contracts::DeviceControlContracts::PadColourIds;
using PadAction      = contracts::DeviceControlContracts::PadAction;

// ElfKick: no colour-id -> juce::Colour table exists in the released tree
// (the old mt::Kick::getPadColourFromId is gone), so map by colour name here.
inline juce::Colour getPadColourFromId( PadColourIds id )
{
    using namespace contracts;
    switch( id )
    {
        case DeviceControlContracts::padColourDarkRed:       return juce::Colours::darkred;
        case DeviceControlContracts::padColourVividPurple:   return juce::Colour( 0xff8d38c9 );
        case DeviceControlContracts::padColourDarkYellow:    return juce::Colour( 0xffb58900 );
        case DeviceControlContracts::padColourlightGreen1:   return juce::Colours::lightgreen;
        case DeviceControlContracts::padColourlightGreen3:   return juce::Colour( 0xff32cd32 );
        case DeviceControlContracts::padColourlightGreen4:   return juce::Colour( 0xff00a550 );
        case DeviceControlContracts::PadColourLightSkyBlue:  return juce::Colours::lightskyblue;
        case DeviceControlContracts::PadColourAqua:          return juce::Colours::aqua;
        case DeviceControlContracts::PadColourBlue1:         return juce::Colours::blue;
        case DeviceControlContracts::PadColourPink:          return juce::Colours::pink;
        case DeviceControlContracts::PadColourPink1:         return juce::Colour( 0xffff69b4 );
        case DeviceControlContracts::PadColourPink2:         return juce::Colour( 0xffff1493 );
        case DeviceControlContracts::PadColourRed:           return juce::Colours::red;
        case DeviceControlContracts::PadColourOrange1:       return juce::Colour( 0xffff8c00 );
        case DeviceControlContracts::PadColourOrange:        return juce::Colours::orange;
        case DeviceControlContracts::PadColourTrans:         return juce::Colours::transparentBlack;
        case DeviceControlContracts::PadColourNeutralWhite:  return juce::Colours::white;
        case DeviceControlContracts::PadColourNeutralGrey:   return juce::Colours::grey;
    }
    return juce::Colours::transparentBlack;
}

class KickSimulatorButton : public juce::Component
{
public:
    KickSimulatorButton(int pos);
    ~KickSimulatorButton();
    int getPos() { return pos; }
    void paint(juce::Graphics& g);
    void setState(PadButtonState newState) { state = newState; }
    void setPadColour(const juce::Colour& clr) { padColour = clr; }

SIGNALS:
    sigslot::signal<int, bool> padStateChanged;

private:
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

public:
    int pos = 0;
    bool mousePressed = false;
    juce::Colour padColour;
    PadButtonState state = PadButtonState::StateOff;
};

class SimulatorButtonGrid : public juce::Component
{
public:
    SimulatorButtonGrid();
    ~SimulatorButtonGrid();
    void resized() override;
    void paint(juce::Graphics& g) override;
    void setCurrentBank(int cBank);

private:
    SimulatorIntrefaceImpl* impl = nullptr;
    std::list<std::unique_ptr<KickSimulatorButton>> buttons;
    std::vector<PadButtonState> padState;
    int currentBank = 0;
    std::vector<PadColourIds> padColour;
};

class Simulator  : public juce::Component
{
public:
    Simulator ();
    ~Simulator();

    void paint (juce::Graphics& g) override;
    void resized() override;
private:
    juce::Rectangle<int> getSliderRect();
    juce::Rectangle<int> getMasterRect();
    juce::Rectangle<int> getRecRect();
    juce::Rectangle<int> getSliderLinkRect();
    juce::Rectangle<int> getSliderControlsRect();
    juce::Rectangle<int> getQlinkControlsRect();

private:
    SimulatorIntrefaceImpl* sif = nullptr;
    ui::KickImageButton brand{"", "Sim Brands dt_KlarkTeknik.png", ""};
    ui::KickImageButton logo{ "", "Sim KICK logo.png", "" };
    SimulatorButtonGrid pad;
    SimulatorMainControl mainControl;
    SimulatorMainControl1 mainControl1;
    SimTransportControl transport;
    SimQlinkControls qlinks;
    ui::KickSlider slider;
    ui::KickRotary master{"Master"};
    ui::KickRotary rec{ "Rec" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Simulator)
};
}
