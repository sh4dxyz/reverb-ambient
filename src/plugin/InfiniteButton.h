#pragma once

#include "HorizonLookAndFeel.h"

// Big round INFINI toggle: a violet orb that collapses into a black hole with a photon
// ring and a spinning accretion disk when engaged.
class InfiniteButton final : public juce::ToggleButton
{
public:
    InfiniteButton() : juce::ToggleButton ("INFINI") { setClickingTogglesState (true); }

    // Called by the editor's animation timer.
    void advance (float dt)
    {
        const float target = getToggleState() ? 1.0f : 0.0f;
        morph += (target - morph) * juce::jmin (1.0f, dt * 3.0f);
        spin += dt * (0.6f + 2.4f * morph);
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat();
        auto label = b.removeFromBottom (24.0f);
        const float d = jmin (b.getWidth(), b.getHeight()) - 8.0f;
        const auto c = b.getCentre();
        const float r = d * 0.5f * (down ? 0.96f : 1.0f);

        // Outer halo.
        const auto haloCol = Theme::violet.interpolatedWith (Theme::amber, morph);
        ColourGradient halo (haloCol.withAlpha (over ? 0.55f : 0.38f), c.x, c.y, haloCol.withAlpha (0.0f), c.x + r * 1.25f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - r * 1.25f, c.y - r * 1.25f, r * 2.5f, r * 2.5f);

        // Orb (off) fading into the event horizon (on).
        ColourGradient orb (Colour (0xffd9c8ff).interpolatedWith (Colours::black, morph), c.x - r * 0.3f, c.y - r * 0.35f,
                            Colour (0xff3a1c88).interpolatedWith (Colours::black, morph), c.x + r, c.y + r, true);
        g.setGradientFill (orb);
        g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);

        // Accretion disk arcs.
        if (morph > 0.01f)
        {
            for (int i = 0; i < 3; ++i)
            {
                Path arc;
                const float rr = r * (0.80f + 0.07f * (float) i);
                const float a0 = spin * (1.0f + 0.35f * (float) i) + (float) i * 2.1f;
                arc.addCentredArc (c.x, c.y, rr, rr * 0.38f, -0.25f, a0, a0 + 2.2f, true);
                g.setColour (Theme::amber.interpolatedWith (Theme::gold, (float) i * 0.5f).withAlpha (0.75f * morph));
                g.strokePath (arc, PathStrokeType (2.2f - 0.5f * (float) i, PathStrokeType::curved, PathStrokeType::rounded));
            }
            // Photon ring.
            g.setColour (Theme::gold.withAlpha (0.9f * morph));
            g.drawEllipse (c.x - r * 0.74f, c.y - r * 0.74f, r * 1.48f, r * 1.48f, 1.6f);
        }

        // Infinity glyph.
        {
            Path inf;
            const float w = r * 0.62f, h = r * 0.30f;
            inf.startNewSubPath (c);
            inf.cubicTo (c.x + w * 0.4f, c.y - h * 1.3f, c.x + w, c.y - h * 0.4f, c.x + w, c.y);
            inf.cubicTo (c.x + w, c.y + h * 0.4f, c.x + w * 0.4f, c.y + h * 1.3f, c.x, c.y);
            inf.cubicTo (c.x - w * 0.4f, c.y - h * 1.3f, c.x - w, c.y - h * 0.4f, c.x - w, c.y);
            inf.cubicTo (c.x - w, c.y + h * 0.4f, c.x - w * 0.4f, c.y + h * 1.3f, c.x, c.y);
            g.setColour (Colours::white.withAlpha (0.18f + 0.2f * morph));
            g.strokePath (inf, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colours::white.interpolatedWith (Theme::gold, morph));
            g.strokePath (inf, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        g.setFont (Theme::labelFont (17.0f));
        g.setColour (getToggleState() ? Theme::gold : Theme::inkDim);
        g.drawText ("INFINI", label, Justification::centred);
    }

private:
    float morph = 0.0f, spin = 0.0f;
};
