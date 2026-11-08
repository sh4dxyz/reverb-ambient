#pragma once

#include "Parameters.h"
#include "dsp/AmbientReverb.h"
#include "dsp/TailRecorder.h"

#include <juce_audio_processors/juce_audio_processors.h>

class HorizonNoirProcessor final : public juce::AudioProcessor
{
public:
    HorizonNoirProcessor();
    ~HorizonNoirProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 60.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }
    const rvb::TailRecorder& getRecorder() const { return recorder; }

    // Levels for the visuals (written by the audio thread, read by the UI).
    struct Meters
    {
        std::atomic<float> input { 0.0f }, wet { 0.0f }, low { 0.0f }, high { 0.0f };
    };
    const Meters& getMeters() const { return meters; }

private:
    rvb::Params readParams() const;

    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pShimmer = nullptr;
    std::atomic<float>* pDucking = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pInfinite = nullptr;

    rvb::AmbientReverb reverb;
    rvb::TailRecorder recorder;
    juce::AudioBuffer<float> wetBuffer;
    Meters meters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonNoirProcessor)
};
