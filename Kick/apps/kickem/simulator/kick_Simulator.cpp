#include "kick_Simulator.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace sim
{
#define padSpacing  20
#define TOTAL_DRUM_PADS 16

namespace
{
// ElfKick pad-gate runtime switch (Garth, Oct 28 2026). Upstream's pad
// widget suppresses input while a pad is PadButtonState::StateOff; on the
// real device the MCU sends pad packets unconditionally, so that gate is
// simulator-only. Both behaviours stay available:
//   ELFKICK_PADS=always   (default) pads emit on click regardless of state
//   ELFKICK_PADS=upstream exact original StateOff early-return behaviour
// Read once, lazily on first use — SimulatorButtonGrid's ctor calls this
// at panel construction so the active mode is logged at startup.
bool elfKickPadsAlwaysEmit()
{
    static const bool alwaysEmit = []
    {
        const char* mode = std::getenv("ELFKICK_PADS");
        const bool upstream = (mode != nullptr && std::string(mode) == "upstream");
        std::cout << "ElfKick: ELFKICK_PADS=" << (mode != nullptr ? mode : "(unset)")
                  << " -> pad gate mode: "
                  << (upstream ? "upstream (StateOff gate active)"
                               : "always (pads emit regardless of state)")
                  << std::endl;
        return ! upstream;
    }();
    return alwaysEmit;
}
} // namespace

KickSimulatorButton::KickSimulatorButton(int p)
    :pos(p)
{
}

KickSimulatorButton::~KickSimulatorButton()
{
}

void KickSimulatorButton::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(34, 34, 34));
    g.fillAll();

    juce::Colour clr;
    if (mousePressed)
        clr = padColour.withAlpha(0.75f);
    else if (state == PadButtonState::StateOff)
        clr = juce::Colours::transparentBlack;
    else
        clr = padColour;

    g.setColour(clr);
    g.drawRect(getLocalBounds(), 3);
}

void KickSimulatorButton::mouseDown(const juce::MouseEvent&)
{
    // ElfKick: pad gate is runtime-switchable (see elfKickPadsAlwaysEmit
    // above). In "upstream" mode the condition below reduces to the exact
    // original `state == PadButtonState::StateOff` early return; in the
    // default "always" mode pads emit regardless of state, matching the
    // real device whose MCU sends pad packets unconditionally.
    // paint() still reflects StateOff visually in both modes.
    if (! elfKickPadsAlwaysEmit() && state == PadButtonState::StateOff)
        return;

    padStateChanged(pos, true);
    mousePressed = true;
    repaint();
}

void KickSimulatorButton::mouseUp(const juce::MouseEvent&)
{
    // ElfKick: same switch as mouseDown — release is gated identically.
    if (! elfKickPadsAlwaysEmit() && state == PadButtonState::StateOff)
        return;

    padStateChanged(pos, false);
    mousePressed = false;
    repaint();
}

SimulatorButtonGrid::SimulatorButtonGrid()
{
    // ElfKick: resolve the pad-gate switch now so the active mode is
    // logged at startup, not on the first pad press.
    (void) elfKickPadsAlwaysEmit();

    padColour.resize(128, PadColourIds::PadColourTrans);
    padState.resize(128, PadButtonState::StateOff);
    auto triggerHwEvent = [this](int pos, bool pressed)
    {
        std::vector<char> data(2);
        int pad = SIM_PAD_ID_BASE + pos;
        data[1] = 127; //velocity
        // ElfKick: PAD_PRESSED/PAD_RELEASED -> contracts BtnState values.
        if (pressed)
            data[0] = (char)contracts::DeviceControlContracts::BTN_PRESSED;
        else
            data[0] = (char)contracts::DeviceControlContracts::BTN_RELEASED;

        impl->hwInputReceived(pad, data);
    };

    impl = new SimulatorIntrefaceImpl();
    for (int row = 0; row < 4; row++)
    {
        for (int column = 0; column < 4; column++)
        {
            auto& padBtn = buttons.emplace_back(std::unique_ptr<KickSimulatorButton>(new KickSimulatorButton( row * 4 + column)));
            addAndMakeVisible(*padBtn);
            padBtn->padStateChanged.connect(triggerHwEvent);
        }
    }

    impl->hwMessageReceived.connect([this](int hwId, const std::vector<juce::int8>& data)
    {
        if (!(hwId >= SIM_PAD_ID_BASE && hwId < SIM_PAD_ID_BASE + TOTAL_DRUM_PADS))
            return;
        if (data.size() < 2)
            return;

        PadAction pdAction = (PadAction)data[0];
        int pad = hwId - SIM_PAD_ID_BASE;
        if (pdAction == PadAction::SetPadColour)
        {
            if (data.size() < 3)
                return;
            int bank = (int)data[1];

            PadColourIds selPadClr = (PadColourIds)data[2];
            padColour[(size_t)bank * TOTAL_DRUM_PADS + pad] = selPadClr;

            if (currentBank == bank)
            {
                for (auto& button : buttons)
                {
                    if (button->getPos() == pad)
                    {
                        button->setPadColour(getPadColourFromId(selPadClr));
                        break;
                    }
                }
            }
            repaint();
        }
        else if (pdAction == PadAction::SetPadState)
        {
            padState[currentBank * TOTAL_DRUM_PADS + pad] = (PadButtonState)data[1];
            setCurrentBank(currentBank);
        }
    });
}

SimulatorButtonGrid::~SimulatorButtonGrid()
{
}

void SimulatorButtonGrid::resized()
{
    int buttonWidth = (int)(std::round(getWidth() - (5 * padSpacing)) / 4);
    int buttonHeight = (int)(std::round(getHeight() - (5 * padSpacing)) / 4);
    int yPos = getHeight() - padSpacing - buttonHeight;
    int xPos = padSpacing;

    int pos = 0;
    for (std::unique_ptr<KickSimulatorButton>& padBtn : buttons)
    {
        padBtn->setBounds(xPos, yPos, buttonWidth, buttonHeight);
        xPos += buttonWidth + padSpacing;
        pos++;
        if (pos / 4 == 1)
        {
            yPos -= buttonHeight + padSpacing;
            pos = 0;
            xPos = padSpacing;
        }
    }
}

void SimulatorButtonGrid::setCurrentBank(int cBank)
{
    currentBank = cBank;
    int buttonStartValue = cBank * 16;
    for (auto& padButton : buttons)
    {
        padButton->setPadColour(getPadColourFromId(padColour[buttonStartValue]));
        padButton->setState(padState[buttonStartValue]);
        buttonStartValue++;
    }

    repaint();
}

void SimulatorButtonGrid::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(40, 39, 37));
    g.fillAll();

    g.setColour(juce::Colours::white);
    int buttonWidth = (int)(std::round(getWidth() - (5 * padSpacing)) / 4);
    int buttonHeight = (int)(std::round(getHeight() - (5 * padSpacing)) / 4);
    int yPos = getHeight() - padSpacing;
    int xPos = padSpacing;

    int pos = 0;
    for (std::unique_ptr<KickSimulatorButton>& padBtn : buttons)
    {
        juce::Rectangle<int> textRect(xPos, yPos, buttonWidth, 16);
        juce::String text = juce::String().formatted("Pad %d", padBtn->pos);
        g.drawText(text, textRect, juce::Justification::centredLeft);

        xPos += buttonWidth + padSpacing;
        pos++;
        if (pos / 4 == 1)
        {
            yPos -= buttonHeight + padSpacing;
            pos = 0;
            xPos = padSpacing;
        }
    }
}

Simulator::Simulator()
{
    sif = new SimulatorIntrefaceImpl;
    // ElfKick: JUCE 7 Displays API — getPrimaryDisplay() returns by value.
    const auto primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (primary != nullptr)
        setBounds(primary->userArea);

    slider.setRange(1, 100, 1);
    addAndMakeVisible(master);
    master.setDisplayProportions(.6f, .2f, .2f);
    master.showTitleAndValue(true, juce::Slider::TextBoxBelow, juce::Justification::centred);

    addAndMakeVisible(rec);
    rec.setDisplayProportions(.6f, .2f, .2f);
    rec.showTitleAndValue(true, juce::Slider::TextBoxBelow, juce::Justification::centred);

    addAndMakeVisible(slider);
    addAndMakeVisible(brand);
    addAndMakeVisible(logo);
    // ElfKick: AppHome is now kick_ui::KickHome, a reference singleton.
    addAndMakeVisible(kick_ui::KickHome::Instance());
    addAndMakeVisible(pad);
    addAndMakeVisible(mainControl);
    addAndMakeVisible(mainControl1);
    addAndMakeVisible(transport);
    addAndMakeVisible(qlinks);
    mainControl.bankChanged.connect([this](int bankEnum)
        {
            // BtnLedSequence banks are contiguous: EN_BANK_A..EN_BANK_H = 0..7.
            pad.setCurrentBank(bankEnum - (int)contracts::DeviceControlContracts::EN_BANK_A);
        }
    );
}

Simulator::~Simulator()
{
    delete sif;
}

void Simulator::paint(juce::Graphics &g)
{
    g.setColour(juce::Colour(53, 53, 53));
    g.fillAll();

    g.setColour(juce::Colour(27, 27, 27));
    g.fillRect(getSliderRect());
    g.fillRect(getMasterRect());
    g.fillRect(getRecRect());
    g.fillRect(getSliderLinkRect());
    g.fillRect(getSliderControlsRect());
    g.fillRect(getQlinkControlsRect());
}

void Simulator::resized()
{
    brand.setBounds(20, 20, 275, 37);
    logo.setBounds(20, brand.getBottom() + 10, 275, 50);
    kick_ui::KickHome::Instance().setTopLeftPosition(683, 20);
    pad.setBounds(40, getSliderControlsRect().getBottom() + 10, 614, 490);
    mainControl.setBounds(692, kick_ui::KickHome::Instance().getBottom() + 2, 1018, 458);
    transport.setBounds(1767, 578, 84, 437);
    qlinks.setBounds(1753, 40, 110, 526);

    slider.setBounds(446, 40, 16, 280);
    master.setBounds(512, 30, 148, 137);
    rec.setBounds(512, 197, 148, 137);

    mainControl1.setBounds(79, 395, 442 - 68, 63);
}

juce::Rectangle<int> Simulator::getSliderRect()
{
    return juce::Rectangle<int>(335, 20, 147, 324);
}

juce::Rectangle<int> Simulator::getMasterRect()
{
    return juce::Rectangle<int>(502, 20, 168, 157);
}

juce::Rectangle<int> Simulator::getRecRect()
{
    return juce::Rectangle<int>(502, 187, 168, 157);
}

juce::Rectangle<int> Simulator::getSliderLinkRect()
{
    return juce::Rectangle<int>(502, 354, 168, 156);
}

juce::Rectangle<int> Simulator::getSliderControlsRect()
{
    return juce::Rectangle<int>(40, 354, 442, 156);
}

juce::Rectangle<int> Simulator::getQlinkControlsRect()
{
    return juce::Rectangle<int>(1734, 20, 147, 524);
}

}
