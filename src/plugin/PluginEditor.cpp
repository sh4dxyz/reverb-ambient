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
    setupKnob (size, ParamIDs::size, "Taille", Theme::violet, utf8 ("Taille de l'espace : de la crypte \xc3\xa0 la cath\xc3\xa9" "drale."));
    setupKnob (decay, ParamIDs::decay, utf8 ("Dur\xc3\xa9" "e"), Theme::magenta, utf8 ("Dur\xc3\xa9" "e de la queue, de 1 \xc3\xa0 60 secondes."));
    setupKnob (tone, ParamIDs::tone, "Sombre / Clair", Theme::amber, utf8 ("Vitesse \xc3\xa0 laquelle les aigus s'\xc3\xa9teignent dans la queue."));
    setupKnob (shimmer, ParamIDs::shimmer, "Shimmer", Theme::cyan, utf8 ("Transposition r\xc3\xa9inject\xc3\xa9" "e dans la reverb (-12 \xc3\xa0 +12 demi-tons)."));
    setupKnob (ducking, ParamIDs::ducking, "Ducking", Theme::violet, utf8 ("Baisse la reverb pendant que tu joues, elle remonte dans les silences."));
    setupKnob (mix, ParamIDs::mix, "Dry / Wet", Theme::magenta, utf8 ("M\xc3\xa9lange signal direct / reverb."));

    infiniteButton.setTooltip (utf8 ("INFINI : la nappe est tenue ind\xc3\xa9" "finiment (les nouveaux sons ne rentrent plus dans la reverb)."));
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

void HorizonNoirEditor::setupKnob (Knob& k, const char* paramId, const juce::String& name, juce::Colour accent, const juce::String& tip)
{
    k.slider.setName (name);
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setMouseDragSensitivity (220);
    k.slider.setVelocityBasedMode (false);
    k.slider.setTooltip (tip);
    HorizonLookAndFeel::setAccent (k.slider, accent);
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

    // Bottom row: three knobs | INFINI | three knobs.
    const int knobW = 112, knobH = 132, rowY = designHeight - knobH - 28;
    const int infW = 150;
    const int gap = 8;
    const int totalW = 6 * knobW + infW + 6 * gap;
    int x = (designWidth - totalW) / 2;
    for (auto* k : { &size, &decay, &tone })
    {
        k->slider.setBounds (x, rowY, knobW, knobH);
        x += knobW + gap;
    }
    infiniteButton.setBounds (x, rowY - 18, infW, knobH + 18);
    x += infW + gap;
    for (auto* k : { &shimmer, &ducking, &mix })
    {
        k->slider.setBounds (x, rowY, knobW, knobH);
        x += knobW + gap;
    }

    dragZone.setBounds (designWidth - 36 - 330, 30, 330, 58);
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

    infiniteButton.advance (dt);
    dragZone.advance (dt, level);

    if (! renderer.isWorking())
        repaint();
}

void HorizonNoirEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    // Title block.
    g.setFont (Theme::titleFont (30.0f));
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawText ("HORIZON NOIR", 38, 36, 520, 40, Justification::left);
    g.setColour (Colours::white);
    g.drawText ("HORIZON NOIR", 36, 34, 520, 40, Justification::left);
    g.setFont (Theme::labelFont (14.0f, false));
    g.setColour (Theme::inkDim);
    g.drawText (String::fromUTF8 ("AMBIENT REVERB  \xc2\xb7  FDN 16 LIGNES  \xc2\xb7  SHIMMER  \xc2\xb7  INFINI"), 38, 74, 520, 20, Justification::left);

    // Soft dark floor so the controls read on any part of the nebula.
    ColourGradient floor (Colour (0x00000000), 0.0f, (float) getHeight() - 230.0f, Colour (0xb0030208), 0.0f, (float) getHeight(), false);
    g.setGradientFill (floor);
    g.fillRect (0, getHeight() - 230, getWidth(), 230);
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
