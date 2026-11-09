#pragma once

#include "HorizonLookAndFeel.h"

// FREEZE latch: a plain rectangular key with a small amber LED.
class InfiniteButton final : public juce::ToggleButton
{
public:
    InfiniteButton() : juce::ToggleButton ("FREEZE") { setClickingTogglesState (true); }

    // Called by the editor's animation timer (soft LED fade).
    void advance (float dt)
    {
        const float target = getToggleState() ? 1.0f : 0.0f;
        const float before = led;
        led += (target - led) * juce::jmin (1.0f, dt * 10.0f);
        if (std::abs (led - before) > 0.002f) repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        if (down) b = b.translated (0.0f, 1.0f);

        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (0.0f, 2.0f), 3.0f);
        g.setColour (Theme::knob.brighter (over ? 0.08f : 0.0f));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (Theme::knobRim);
        g.drawRoundedRectangle (b, 3.0f, 1.0f);

        // LED.
        const float r = 3.5f;
        const Point<float> c (b.getX() + 14.0f, b.getCentreY());
        if (led > 0.01f)
        {
            g.setColour (Theme::accent.withAlpha (0.25f * led));
            g.fillEllipse (c.x - r * 2.2f, c.y - r * 2.2f, r * 4.4f, r * 4.4f);
        }
        g.setColour (Colour (0xff2a2520).interpolatedWith (Theme::accent.brighter (0.2f), led));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        g.setFont (Theme::labelFont (14.0f));
        g.setColour (Theme::inkDim.interpolatedWith (Theme::ink, led));
        g.drawText ("FREEZE", b.withTrimmedLeft (24.0f), Justification::centred);
    }

private:
    float led = 0.0f;
};
