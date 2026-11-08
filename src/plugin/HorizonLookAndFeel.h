#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Sober "hardware faceplate" look: matte knobs, cream pointer, fine ticks, one warm accent.
namespace Theme
{
inline const juce::Colour ink { 0xffe8e2d4 };     // cream
inline const juce::Colour inkDim { 0xff8d8796 };  // labels
inline const juce::Colour panel { 0xec0c0b10 };   // faceplate
inline const juce::Colour edge { 0xff2b2833 };    // hairlines
inline const juce::Colour knob { 0xff1c1a21 };
inline const juce::Colour knobRim { 0xff3a3742 };
inline const juce::Colour accent { 0xffd9a35b };  // amber

juce::Font titleFont (float height);
juce::Font labelFont (float height, bool semiBold = true);
} // namespace Theme

class HorizonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HorizonLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override { return Theme::labelFont (13.0f); }
};
