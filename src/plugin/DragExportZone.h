#pragma once

#include "HorizonLookAndFeel.h"
#include "dsp/TailRecorder.h"

#include <array>

// Strip showing the latest recorded reverb take. Dragging it out of the plugin writes the
// take to a WAV file and hands it to the host (FL Studio, Ableton...) as an external drag.
class DragExportZone final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragExportZone (const rvb::TailRecorder& recorder);

    // Called by the editor's animation timer with the current wet level (0..1).
    void advance (float dt, float level);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static juce::File getExportFolder();

private:
    juce::File writeTake();

    const rvb::TailRecorder& recorder;
    std::array<float, 96> history {};
    size_t historyPos = 0;
    float historyTimer = 0.0f, phase = 0.0f, flash = 0.0f;
    bool dragStarted = false;
    juce::String lastMessage;
};
