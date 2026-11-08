#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
inline const juce::Colour ink { 0xffeae6ff };
inline const juce::Colour inkDim { 0x99c9c2ee };
inline const juce::Colour glass { 0x5a0b0918 };
inline const juce::Colour glassEdge { 0x40ffffff };
inline const juce::Colour magenta { 0xffff3d9a };
inline const juce::Colour violet { 0xff8f62ff };
inline const juce::Colour cyan { 0xff3fd8ff };
inline const juce::Colour amber { 0xffffb35c };
inline const juce::Colour gold { 0xffffd89a };

juce::Font titleFont (float height);
juce::Font labelFont (float height, bool semiBold = true);
} // namespace Theme

class HorizonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HorizonLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override { return Theme::labelFont (15.0f); }

    static void setAccent (juce::Component& c, juce::Colour colour) { c.getProperties().set ("accent", (int) colour.getARGB()); }
    static juce::Colour getAccent (const juce::Component& c)
    {
        const auto v = c.getProperties()["accent"];
        return v.isVoid() ? Theme::violet : juce::Colour ((juce::uint32) (int) v);
    }
};
