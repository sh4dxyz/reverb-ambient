#include "HorizonLookAndFeel.h"
#include "BinaryData.h"

namespace Theme
{
static juce::Typeface::Ptr loadTypeface (const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor (data, size);
}

juce::Font titleFont (float height)
{
    static auto tf = loadTypeface (BinaryData::SyncopateBold_ttf, BinaryData::SyncopateBold_ttfSize);
    return juce::Font (juce::FontOptions (tf).withHeight (height).withKerningFactor (0.18f));
}

juce::Font labelFont (float height, bool semiBold)
{
    static auto semi = loadTypeface (BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize);
    static auto medium = loadTypeface (BinaryData::RajdhaniMedium_ttf, BinaryData::RajdhaniMedium_ttfSize);
    return juce::Font (juce::FontOptions (semiBold ? semi : medium).withHeight (height).withKerningFactor (0.08f));
}
} // namespace Theme

HorizonLookAndFeel::HorizonLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Theme::ink);
    setColour (juce::Label::textColourId, Theme::ink);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf5121117));
    setColour (juce::TooltipWindow::textColourId, Theme::ink);
    setColour (juce::TooltipWindow::outlineColourId, Theme::edge);
}

void HorizonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;
    const bool active = slider.isMouseOverOrDragging();
    const float labelH = 18.0f;

    auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    auto label = bounds.removeFromBottom (labelH);
    const float diameter = jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    const auto c = bounds.getCentre();
    const float tickR = diameter * 0.5f;
    const float bodyR = tickR - 7.0f;

    // Scale ticks (detents for stepped parameters such as Shimmer).
    const double interval = slider.getInterval();
    const auto range = slider.getRange();
    int numTicks = 11;
    if (interval > 0.0 && range.getLength() / interval <= 12.0)
        numTicks = (int) std::round (range.getLength() / interval) + 1;
    if (slider.getProperties().contains ("detents"))
        numTicks = (int) slider.getProperties()["detents"];
    for (int i = 0; i < numTicks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (numTicks - 1);
        const auto p1 = c.getPointOnCircumference (tickR - 3.0f, a);
        const auto p2 = c.getPointOnCircumference (tickR, a);
        g.setColour (Theme::knobRim.brighter (0.15f));
        g.drawLine ({ p1, p2 }, 1.0f);
    }

    // Value arc: a thin amber line hugging the body (bipolar values grow from the top).
    const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, bodyR + 3.0f, bodyR + 3.0f, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (Theme::accent.withAlpha (active ? 1.0f : 0.8f));
        g.strokePath (arc, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::butt));
    }

    // Matte body with a faint top highlight and a 1 px rim.
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - bodyR, c.y - bodyR + 2.0f, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (Theme::knob);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (Colours::white.withAlpha (0.035f));
    g.fillEllipse (c.x - bodyR * 0.8f, c.y - bodyR * 0.9f, bodyR * 1.6f, bodyR * 1.0f);
    g.setColour (active ? Theme::knobRim.brighter (0.3f) : Theme::knobRim);
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // Pointer.
    g.setColour (Theme::ink);
    g.drawLine ({ c.getPointOnCircumference (bodyR * 0.35f, angle), c.getPointOnCircumference (bodyR * 0.88f, angle) }, 2.0f);

    // Label, replaced by the value while the knob is touched.
    g.setFont (Theme::labelFont (12.5f));
    g.setColour (active ? Theme::ink : Theme::inkDim);
    g.drawText (active ? slider.getTextFromValue (slider.getValue()) : slider.getName().toUpperCase(), label, Justification::centred);
}
