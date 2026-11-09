#include "PluginEditor.h"

namespace
{
juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

// Maps an RMS value to 0..1 on a 60 dB scale.
float toVisual (float rms)
{
    const float db = 20.0f * std::log10 (juce::jmax (rms, 1.0e-6f));
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

void follow (float& state, float target, float attack, float release)
{
    state += (target - state) * (target > state ? attack : release);
}
} // namespace

HorizonNoirEditor::HorizonNoirEditor (HorizonNoirProcessor& p)
    : AudioProcessorEditor (p), processor (p), dragZone (p.getRecorder())
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    addAndMakeVisible (content);
    setupKnob (size, ParamIDs::size, "TAILLE", utf8 ("Taille de l'espace, de la crypte \xc3\xa0 la cath\xc3\xa9" "drale. La queue en cours garde sa taille, les nouveaux sons prennent la nouvelle."));
    setupKnob (decay, ParamIDs::decay, utf8 ("DUR\xc3\x89" "E"), utf8 ("Dur\xc3\xa9" "e de la queue, de 1 \xc3\xa0 60 secondes."));
    setupKnob (tone, ParamIDs::tone, "SOMBRE/CLAIR", utf8 ("Vitesse \xc3\xa0 laquelle les aigus s'\xc3\xa9teignent dans la queue."));
    setupKnob (shimmer, ParamIDs::shimmer, "SHIMMER", utf8 ("Halo transpos\xc3\xa9 r\xc3\xa9inject\xc3\xa9 dans la reverb : octave, quinte ou quarte, vers le haut ou le bas."));
    shimmer.slider.getProperties().set ("detents", 7);
    setupKnob (ducking, ParamIDs::ducking, "DUCKING", utf8 ("Baisse la reverb pendant que tu joues, elle remonte dans les silences."));
    setupKnob (mix, ParamIDs::mix, "DRY/WET", utf8 ("M\xc3\xa9lange signal direct / reverb."));

    infiniteButton.setTooltip (utf8 ("INFINI : fige la nappe telle quelle, ind\xc3\xa9" "finiment. Les nouveaux sons passent en direct sans s'y ajouter."));
    content.addAndMakeVisible (infiniteButton);
    infiniteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getState(), ParamIDs::infinite, infiniteButton);

    content.addAndMakeVisible (dragZone);

    glContext.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    glContext.setRenderer (&renderer);
    glContext.setComponentPaintingEnabled (true);
    glContext.setContinuousRepainting (true);
    glContext.attachTo (*this);

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) designWidth / (double) designHeight);
        c->setSizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 2, designHeight * 2);
    }
    setSize (designWidth, designHeight);
    startTimerHz (60);
}

HorizonNoirEditor::~HorizonNoirEditor()
{
    stopTimer();
    glContext.detach();
    setLookAndFeel (nullptr);
}

void HorizonNoirEditor::setupKnob (Knob& k, const char* paramId, const juce::String& name, const juce::String& tip)
{
    k.slider.setName (name);
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setMouseDragSensitivity (200);
    k.slider.setVelocityBasedMode (false);
    k.slider.setTooltip (tip);
    content.addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getState(), paramId, k.slider);
}

void HorizonNoirEditor::paint (juce::Graphics& g)
{
    // Drawn by OpenGL; this only shows if the GL context is unavailable.
    if (! renderer.isWorking())
    {
        juce::ColourGradient bg (juce::Colour (0xff1a0c2a), (float) getWidth() * 0.5f, (float) getHeight() * 0.4f,
                                 juce::Colour (0xff040308), 0.0f, (float) getHeight(), true);
        g.setGradientFill (bg);
        g.fillAll();
    }
}

void HorizonNoirEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    content.setBounds (0, 0, designWidth, designHeight);
    content.setTransform (juce::AffineTransform::scale (scale));

    // Faceplate along the bottom: three knobs | INFINI | three knobs.
    const int knobW = 78, knobH = 86, rowY = designHeight - knobH - 18;
    const int infW = 96, gap = 6;
    const int totalW = 6 * knobW + infW + 2 * 14 + 4 * gap;
    int x = (designWidth - totalW) / 2;
    for (auto* k : { &size, &decay, &tone })
    {
        k->slider.setBounds (x, rowY, knobW, knobH);
        x += knobW + gap;
    }
    x += 14 - gap;
    infiniteButton.setBounds (x, rowY + 24, infW, 30);
    x += infW + 14;
    for (auto* k : { &shimmer, &ducking, &mix })
    {
        k->slider.setBounds (x, rowY, knobW, knobH);
        x += knobW + gap;
    }

    dragZone.setBounds (designWidth - 20 - 236, 18, 236, 34);
}

void HorizonNoirEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;

    const auto& m = processor.getMeters();
    follow (level, toVisual (m.wet.load()), 0.35f, 0.05f);
    follow (low, toVisual (m.low.load()), 0.4f, 0.06f);
    follow (high, toVisual (m.high.load()), 0.5f, 0.08f);
    follow (input, juce::jlimit (0.0f, 1.0f, m.input.load() * 2.0f), 0.5f, 0.1f);

    auto& st = processor.getState();
    const bool inf = st.getRawParameterValue (ParamIDs::infinite)->load() > 0.5f;
    infMorph += ((inf ? 1.0f : 0.0f) - infMorph) * juce::jmin (1.0f, dt * 1.6f);

    auto& v = renderer.visuals;
    v.level.store (level);
    v.low.store (low);
    v.high.store (high);
    v.input.store (input);
    v.size.store (st.getRawParameterValue (ParamIDs::size)->load());
    v.decay.store (std::log (st.getRawParameterValue (ParamIDs::decay)->load()) / std::log (60.0f));
    v.tone.store (st.getRawParameterValue (ParamIDs::tone)->load());
    v.shimmer.store (st.getRawParameterValue (ParamIDs::shimmer)->load() / 12.0f);
    v.duck.store (st.getRawParameterValue (ParamIDs::ducking)->load());
    v.infinite.store (infMorph);

    // Animation phases advance by speed x dt (wrapped to keep float precision).
    const float decayNorm = v.decay.load();
    const float flowSpeed = (0.07f + (0.018f - 0.07f) * decayNorm) * (1.0f - 0.6f * infMorph);
    flow = std::fmod (flow + flowSpeed * dt, 1000.0f);
    swirl = std::fmod (swirl + 0.12f * infMorph * dt, juce::MathConstants<float>::twoPi);
    v.flow.store (flow);
    v.swirl.store (swirl);

    infiniteButton.advance (dt);
    dragZone.advance (dt, level);

    if (! renderer.isWorking())
        repaint();
}

void HorizonNoirEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    // Title.
    g.setFont (Theme::titleFont (15.0f));
    g.setColour (Theme::ink);
    g.drawText ("D33P", 22, 18, 300, 20, Justification::left);
    g.setFont (Theme::labelFont (11.5f, false));
    g.setColour (Theme::inkDim);
    g.drawText ("AMBIENT REVERB", 23, 37, 300, 14, Justification::left);

    // Faceplate.
    const float top = (float) getHeight() - 122.0f;
    g.setColour (Theme::panel);
    g.fillRect (0.0f, top, (float) getWidth(), (float) getHeight() - top);
    g.setColour (Theme::edge);
    g.drawHorizontalLine ((int) top, 0.0f, (float) getWidth());

    // Hairline dividers around INFINI.
    const auto& inf = owner.infiniteButton.getBounds();
    g.setColour (Theme::edge);
    g.drawVerticalLine (inf.getX() - 8, top + 16.0f, (float) getHeight() - 16.0f);
    g.drawVerticalLine (inf.getRight() + 7, top + 16.0f, (float) getHeight() - 16.0f);
}

void HorizonNoirEditor::Content::mouseDown (const juce::MouseEvent& e)
{
    owner.renderer.addRipple (e.position.x / (float) getWidth(), e.position.y / (float) getHeight());
}

void HorizonNoirEditor::Content::mouseMove (const juce::MouseEvent& e)
{
    owner.renderer.visuals.mouseX.store (e.position.x / (float) getWidth());
    owner.renderer.visuals.mouseY.store (e.position.y / (float) getHeight());
}

void HorizonNoirEditor::Content::mouseExit (const juce::MouseEvent&)
{
    owner.renderer.visuals.mouseX.store (-1000.0f);
    owner.renderer.visuals.mouseY.store (-1000.0f);
}
