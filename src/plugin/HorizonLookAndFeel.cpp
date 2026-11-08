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
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0100c1c));
    setColour (juce::TooltipWindow::textColourId, Theme::ink);
    setColour (juce::TooltipWindow::outlineColourId, Theme::glassEdge);
}

static void strokeGlowArc (juce::Graphics& g, juce::Point<float> c, float radius, float from, float to,
                           juce::Colour colour, float thickness)
{
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, from, to, true);
    // Soft bloom: wide translucent strokes under the crisp one.
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (colour.withAlpha (0.06f * (float) (5 - i)));
        g.strokePath (arc, juce::PathStrokeType (thickness + (float) i * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void HorizonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;
    const auto accent = getAccent (slider);
    const bool active = slider.isMouseOverOrDragging();
    const float labelH = 22.0f;

    auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    auto knobArea = bounds.withTrimmedBottom (labelH).reduced (6.0f);
    const float diameter = jmin (knobArea.getWidth(), knobArea.getHeight());
    const auto c = knobArea.getCentre();
    const float radius = diameter * 0.5f;
    const float ringR = radius - 7.0f;

    // Glass disc.
    {
        ColourGradient fill (Colour (0x66221a3a), c.x, c.y - radius, Colour (0x5a05040c), c.x, c.y + radius, false);
        g.setGradientFill (fill);
        g.fillEllipse (c.x - ringR + 4.0f, c.y - ringR + 4.0f, (ringR - 4.0f) * 2.0f, (ringR - 4.0f) * 2.0f);
        g.setColour (Theme::glassEdge.withMultipliedAlpha (active ? 1.3f : 0.8f));
        g.drawEllipse (c.x - ringR + 4.0f, c.y - ringR + 4.0f, (ringR - 4.0f) * 2.0f, (ringR - 4.0f) * 2.0f, 1.0f);
    }

    // Track.
    {
        Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Value arc: bipolar parameters grow from the top.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.01f)
        strokeGlowArc (g, c, ringR, jmin (from, angle), jmax (from, angle), accent.withAlpha (active ? 1.0f : 0.9f), 3.0f);

    // Indicator dot.
    const auto dot = c.getPointOnCircumference (ringR, angle);
    g.setColour (accent.withAlpha (0.35f));
    g.fillEllipse (dot.x - 7.0f, dot.y - 7.0f, 14.0f, 14.0f);
    g.setColour (Colours::white);
    g.fillEllipse (dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);

    // Value text inside.
    g.setColour (active ? Colours::white : Theme::ink.withAlpha (0.85f));
    g.setFont (Theme::labelFont (jlimit (12.0f, 18.0f, diameter * 0.17f)));
    g.drawFittedText (slider.getTextFromValue (slider.getValue()), knobArea.reduced (ringR * 0.35f).toNearestInt(),
                      Justification::centred, 2);

    // Name below.
    g.setColour (active ? accent.brighter (0.4f) : Theme::inkDim);
    g.setFont (Theme::labelFont (15.0f));
    g.drawText (slider.getName().toUpperCase(), bounds.removeFromBottom (labelH), Justification::centred);
}
