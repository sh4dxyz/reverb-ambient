#pragma once

#include "BlackHoleAnimator.h"
#include "DragExportZone.h"
#include "HorizonLookAndFeel.h"
#include "InfiniteButton.h"
#include "NebulaRenderer.h"
#include "PluginProcessor.h"

#include <juce_opengl/juce_opengl.h>

// The editor is laid out at a fixed design size (720 x 440) inside `content`, which is
// scaled to the window: the whole UI (and the OpenGL nebula behind it) resizes smoothly
// for screen captures.
class HorizonNoirEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HorizonNoirEditor (HorizonNoirProcessor&);
    ~HorizonNoirEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 720;
    static constexpr int designHeight = 440;

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content (HorizonNoirEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent& e) override { mouseMove (e); }
        void mouseExit (const juce::MouseEvent&) override;

    private:
        HorizonNoirEditor& owner;
    };

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const char* paramId, const juce::String& name, const juce::String& tip);

    HorizonNoirProcessor& processor;
    HorizonLookAndFeel lookAndFeel;
    juce::OpenGLContext glContext;
    NebulaRenderer renderer { glContext };

    Content content { *this };
    Knob size, decay, damping, shimmer, ducking, mix;
    InfiniteButton infiniteButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> infiniteAttachment;
    DragExportZone dragZone;
    juce::TooltipWindow tooltips { this, 600 };

    float level = 0, low = 0, high = 0, input = 0, flow = 0, swirl = 0;
    BlackHoleAnimator blackHole;
    double lastTick = juce::Time::getMillisecondCounterHiRes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonNoirEditor)
};
