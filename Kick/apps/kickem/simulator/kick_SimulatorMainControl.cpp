#include "kick_SimulatorMainControl.h"
#include "kick_SimulatorInterfaceImpl.h"
#include <contracts/dcl/dcl_contracts.h>

#define MAIN_CTRL_BUTTON_WIDTH 93
#define MAIN_CTRL_BUTTON_HEIGHT 63
namespace sim
{
SimulatorMainControl1::SimulatorMainControl1()
{
    addAndMakeVisible(fullLevels);
    fullLevels.addListener(this);
    addAndMakeVisible(levels16);
    levels16.addListener(this);
    addAndMakeVisible(erase);
    erase.addListener(this);
}

void SimulatorMainControl1::resized()
{
    fullLevels.setBounds(0, 0, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    levels16.setBounds(fullLevels.getRight() + 48, 0,  MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    erase.setBounds(levels16.getRight() + 48, 0, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
}

void SimulatorMainControl1::drawSubFunc(juce::Graphics& g, juce::Component& control, const juce::String& subFunctext)
{
    juce::Rectangle<int> subFuncRect(control.getX(), control.getBottom() + 2, control.getWidth(), 16);
    g.drawText(subFunctext, subFuncRect, juce::Justification::centred);
}

void SimulatorMainControl1::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::yellow);
    drawSubFunc(g, fullLevels, "HALF LEVEL");
    drawSubFunc(g, levels16, "NOTES");
}

void SimulatorMainControl1::buttonClicked(juce::Button* button)
{
    if (button == &fullLevels)
    {
        ;
    }
    else if (button == &levels16)
    { 
        ;
    }
    else if (button == &erase)
    {
        ;
    }
}

SimulatorMainControl::SimulatorMainControl()
    : impl(new SimulatorIntrefaceImpl)
{
    addAndMakeVisible(bankA);
    bankA.addListener(this);
    bankA.setRadioGroupId(2);
    addAndMakeVisible(bankB);
    bankB.addListener(this);
    bankB.setRadioGroupId(2);
    addAndMakeVisible(bankC);
    bankC.addListener(this);
    bankC.setRadioGroupId(2);
    addAndMakeVisible(bankD);
    bankD.addListener(this);
    bankD.setRadioGroupId(2);
    bankA.setToggleState(true, juce::dontSendNotification);
    
    mainBtn.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(mainBtn);
    mainBtn.addListener(this);
    mainBtn.setRadioGroupId(1);
    addAndMakeVisible(browseBtn);
    browseBtn.addListener(this);
    browseBtn.setRadioGroupId(1);
    addAndMakeVisible(trackMixBtn);
    trackMixBtn.addListener(this);
    trackMixBtn.setRadioGroupId(1);
    addAndMakeVisible(trackMuteBtn);
    trackMuteBtn.addListener(this);
    trackMuteBtn.setRadioGroupId(1);
    addAndMakeVisible(nextSeqBtn);
    nextSeqBtn.addListener(this);
    nextSeqBtn.setRadioGroupId(1);
    
    addAndMakeVisible(stepSeq);
    stepSeq.addListener(this);
    stepSeq.setRadioGroupId(1);
    addAndMakeVisible(tc);
    tc.addListener(this);
    tc.setRadioGroupId(1);
    addAndMakeVisible(sampler);
    sampler.addListener(this);
    sampler.setRadioGroupId(1);
    addAndMakeVisible(sampleEdit);
    sampleEdit.addListener(this);
    sampleEdit.setRadioGroupId(1);
    addAndMakeVisible(programEdit);
    programEdit.addListener(this);
    programEdit.setRadioGroupId(1);

    addAndMakeVisible(tapTempoBtn);
    tapTempoBtn.addListener(this);
    tapTempoBtn.setRadioGroupId(1);
    addAndMakeVisible(noteRepeatBtn);
    noteRepeatBtn.addListener(this);
    noteRepeatBtn.setRadioGroupId(1);

    addAndMakeVisible(menuBtn);
    menuBtn.addListener(this);
    menuBtn.setRadioGroupId(1);
    addAndMakeVisible(shiftBtn);

    addAndMakeVisible(backBtn);
    backBtn.addListener(this);
    addAndMakeVisible(copyBtn);
    copyBtn.addListener(this);
    addAndMakeVisible(plusBtn);
    plusBtn.addListener(this);
    addAndMakeVisible(minusBtn);
    minusBtn.addListener(this);
    addAndMakeVisible(centralRotary);
    addAndMakeVisible(centralKnobClick);
    centralKnobClick.addListener(this);
    centralRotary.showTitleAndValue(false, juce::Slider::TextBoxBelow);
    centralRotary.setDisplayProportions(.6f, .2f, .2f);
    centralRotary.setSelectionEnabled(false);
    centralRotary.setRange(0, 127);
    centralRotary.onValueChanged = [this](double value)
    {
        std::vector<char> data;
        int currentValue = (int)std::round(value);
        data.push_back((juce::int8)(currentValue - centralRotaryValue));
        centralRotaryValue = currentValue;
        DBG(currentValue);
        if (data[0] < 0)
            impl->hwInputReceived(contracts::DeviceControlContracts::EN_KNOB_CCW, data);
        else
            impl->hwInputReceived(contracts::DeviceControlContracts::EN_KNOB_CW, data);
    };

}

SimulatorMainControl::~SimulatorMainControl()
{
}

void SimulatorMainControl::buttonClicked(juce::Button* button)
{
    if (button == &centralKnobClick)
    {
        impl->hwInputReceived((int)contracts::DeviceControlContracts::EN_KNOB_ENTER, std::vector<char>{});
        return;
    }

    bool shiFtPressed = shiftBtn.getToggleState();
    if (button == &bankA)
    {
        if (shiFtPressed)
            bankChanged(contracts::DeviceControlContracts::EN_BANK_E);
        else
            bankChanged(contracts::DeviceControlContracts::EN_BANK_A);
    }
    else if (button == &bankB)
    {
        if (shiFtPressed)
            bankChanged(contracts::DeviceControlContracts::EN_BANK_F);
        else
            bankChanged(contracts::DeviceControlContracts::EN_BANK_B);
    }
    else if (button == &bankC)
    {
        if (shiFtPressed)
            bankChanged(contracts::DeviceControlContracts::EN_BANK_G);
        else
            bankChanged(contracts::DeviceControlContracts::EN_BANK_C);
    }
    else if (button == &bankD)
    {
        if (shiFtPressed)
            bankChanged(contracts::DeviceControlContracts::EN_BANK_H);
        else
            bankChanged(contracts::DeviceControlContracts::EN_BANK_D);
    }

    if (!button->getToggleState())
        return;

    std::vector<char> data;
    if (shiftBtn.getToggleState())
    {
        if (button == &bankA) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_E, data);
        else if (button == &bankB) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_F, data);
        else if (button == &bankC) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_G, data);
        else if (button == &bankD) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_H, data);
        else if (button == &mainBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_GRID, data);
        else if (button == &browseBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_SAVE, data);
        else if (button == &trackMixBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_PAD_MIX, data);
        else if (button == &trackMuteBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_PAD_MUTE, data);
        else if (button == &nextSeqBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_FX, data);
        else if (button == &tapTempoBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_MASTER, data);
        else if (button == &noteRepeatBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_LATCH, data);
        else if (button == &stepSeq) impl->hwInputReceived(contracts::DeviceControlContracts::EN_AUTOMATION, data);
        else if (button == &tc) impl->hwInputReceived(contracts::DeviceControlContracts::EN_ON_OFF, data);
        else if (button == &sampler) impl->hwInputReceived(contracts::DeviceControlContracts::EN_LOOPER, data);
        else if (button == &sampleEdit) impl->hwInputReceived(contracts::DeviceControlContracts::EN_LINK_EDIT, data);
        else if (button == &programEdit) impl->hwInputReceived(contracts::DeviceControlContracts::EN_MIDI_CONTROL, data);
        else if (button == &menuBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_PREF, data);
        else if (button == &backBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_COPY, data);
    }
    else
    {
        if (button == &bankA) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_A, data);
        else if (button == &bankB) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_B, data);
        else if (button == &bankC) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_C, data);
        else if (button == &bankD) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BANK_D, data);
        else if (button == &mainBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_MAINMODE, data);
        else if (button == &browseBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BROWSE, data);
        else if (button == &trackMixBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_TRACK_MIX, data);
        else if (button == &trackMuteBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_TRACK_MUTE, data);
        else if (button == &nextSeqBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_NEXT_SEQ, data);
        else if (button == &tapTempoBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_TAP_TEMPO, data);
        else if (button == &noteRepeatBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_NOTE_REPEAT, data);
        else if (button == &stepSeq) impl->hwInputReceived(contracts::DeviceControlContracts::EN_STEP_SEQ, data);
        else if (button == &tc) impl->hwInputReceived(contracts::DeviceControlContracts::EN_TC, data);
        else if (button == &sampler) impl->hwInputReceived(contracts::DeviceControlContracts::EN_SAMPLER, data);
        else if (button == &sampleEdit) impl->hwInputReceived(contracts::DeviceControlContracts::EN_SAMPLE_EDIT, data);
        else if (button == &programEdit) impl->hwInputReceived(contracts::DeviceControlContracts::EN_PROG_EDIT, data);
        else if (button == &menuBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_MENU, data);
        else if (button == &backBtn) impl->hwInputReceived(contracts::DeviceControlContracts::EN_BACK, data);
    }
}

void SimulatorMainControl::drawSubFunc(juce::Graphics& g, juce::Component& control, const juce::String& subFunctext)
{
    juce::Rectangle<int> subFuncRect(control.getX(), control.getBottom() + 2, control.getWidth(), 16);
    g.drawText(subFunctext, subFuncRect, juce::Justification::centred);
}

void SimulatorMainControl::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::white);
    drawSubFunc(g, bankA, "E");
    drawSubFunc(g, bankB, "F");
    drawSubFunc(g, bankC, "G");
    drawSubFunc(g, bankD, "H");

    drawSubFunc(g, mainBtn, "GRID");
    drawSubFunc(g, browseBtn, "SAVE");
    drawSubFunc(g, trackMixBtn, "PAD MIX");
    drawSubFunc(g, trackMuteBtn, "PAD MUTE");
    drawSubFunc(g, nextSeqBtn, "FX");
    drawSubFunc(g, tapTempoBtn, "MASTER");
    drawSubFunc(g, noteRepeatBtn, "LATCH");
    
    drawSubFunc(g, stepSeq, "AUTOMATION");
    drawSubFunc(g, tc, "ON/OFF");
    drawSubFunc(g, sampler, "LOOPER");
    drawSubFunc(g, sampleEdit, "LINKEDIT");
    drawSubFunc(g, programEdit, "MIDI CONTROL");

    drawSubFunc(g, menuBtn, "PREFS");

    g.drawLine((float)bankA.getX(), 17.0f, bankB.getX() + (MAIN_CTRL_BUTTON_WIDTH /2.0f), 17.0f);
    g.drawLine((float)bankC.getX() + (MAIN_CTRL_BUTTON_WIDTH / 2.0f), 17.0f, (float)bankD.getRight(), 17.0f);
    juce::Rectangle<int> banksLabelRect(bankB.getX() + (MAIN_CTRL_BUTTON_WIDTH / 2), 0, 123, 34);
    g.drawText("BANKS", banksLabelRect, juce::Justification::centred);
}

void SimulatorMainControl::resized()
{
    mainBtn.setBounds(0, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    tapTempoBtn.setBounds(0, mainBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    noteRepeatBtn.setBounds(0, tapTempoBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    stepSeq.setBounds(0, noteRepeatBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);

    browseBtn.setBounds(mainBtn.getRight() + 37, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    trackMixBtn.setBounds(mainBtn.getRight() + 37, browseBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    trackMuteBtn.setBounds(mainBtn.getRight() + 37, trackMixBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);

    bankA.setBounds(browseBtn.getRight() + 60, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    bankB.setBounds(bankA.getRight() + 30, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    bankC.setBounds(bankB.getRight() + 30, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    bankD.setBounds(bankC.getRight() + 30, 34,  MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    
    tc.setBounds(bankD.getRight() + 50, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    sampler.setBounds(bankD.getRight() + 50, tc.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    sampleEdit.setBounds(bankD.getRight() + 50, sampler.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);

    nextSeqBtn.setBounds(tc.getRight() + 37, 34, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    menuBtn.setBounds(tc.getRight() + 37, nextSeqBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    shiftBtn.setBounds(tc.getRight() + 37, menuBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);
    programEdit.setBounds(tc.getRight() + 37, shiftBtn.getBottom() + 28, MAIN_CTRL_BUTTON_WIDTH, MAIN_CTRL_BUTTON_HEIGHT);

    backBtn.setBounds (trackMixBtn.getRight() + 128,  bankA.getBottom() + 26, 145, 133);
    copyBtn.setBounds (backBtn.getRight(), bankA.getBottom() + 26, 145, 143);
    minusBtn.setBounds(trackMixBtn.getRight() + 128, bankA.getBottom() + 26 + 133, 145, 133);
    plusBtn.setBounds (minusBtn.getRight(), bankA.getBottom() + 26 + 133, 145, 133);

    juce::Rectangle<int> rotaryRect(backBtn.getX() + 36, backBtn.getY() + 23, 220, 220);
    centralRotary.setBounds(rotaryRect);
    centralKnobClick.setBounds(rotaryRect.getCentreX() - 20, rotaryRect.getCentreY() - 20, 40, 40);
}

}
