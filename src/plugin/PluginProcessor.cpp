#include "PluginProcessor.h"
#include "PluginEditor.h"

HorizonNoirProcessor::HorizonNoirProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "D33P", createParameterLayout())
{
    pSize = state.getRawParameterValue (ParamIDs::size);
    pDecay = state.getRawParameterValue (ParamIDs::decay);
    pTone = state.getRawParameterValue (ParamIDs::tone);
    pShimmer = state.getRawParameterValue (ParamIDs::shimmer);
    pDucking = state.getRawParameterValue (ParamIDs::ducking);
    pMix = state.getRawParameterValue (ParamIDs::mix);
    pInfinite = state.getRawParameterValue (ParamIDs::infinite);
}

rvb::Params HorizonNoirProcessor::readParams() const
{
    rvb::Params p;
    p.size = pSize->load();
    p.decaySeconds = pDecay->load();
    p.tone = pTone->load();
    p.shimmerSemis = pShimmer->load();
    p.ducking = pDucking->load();
    p.mix = pMix->load();
    p.infinite = pInfinite->load() > 0.5f;
    return p;
}

void HorizonNoirProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    reverb.prepare (sampleRate);
    reverb.setParams (readParams());
    recorder.prepare (sampleRate);
    wetBuffer.setSize (2, juce::jmax (samplesPerBlock, 512), false, true, false);
}

void HorizonNoirProcessor::reset()
{
    reverb.reset();
}

bool HorizonNoirProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void HorizonNoirProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    if (numOut == 0 || numSamples == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto params = readParams();
    reverb.setParams (params);

    float* left = buffer.getWritePointer (0);
    // Mono input: feed the same signal to both sides of the reverb.
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    if (right != nullptr && numIn == 1)
        juce::FloatVectorOperations::copy (right, left, numSamples);

    float* wetL = wetBuffer.getWritePointer (0);
    float* wetR = wetBuffer.getWritePointer (1);
    const int chunk = wetBuffer.getNumSamples();

    float monoScratch[512];

    for (int pos = 0; pos < numSamples;)
    {
        const int n = juce::jmin (chunk, numSamples - pos);
        if (right != nullptr)
        {
            reverb.process (left + pos, right + pos, left + pos, right + pos, wetL, wetR, n);
        }
        else
        {
            // Mono output: render stereo and fold down in small slices.
            for (int s = 0; s < n;)
            {
                const int m = juce::jmin (512, n - s);
                reverb.process (left + pos + s, nullptr, left + pos + s, monoScratch, wetL + s, wetR + s, m);
                for (int i = 0; i < m; ++i)
                    left[pos + s + i] = 0.5f * (left[pos + s + i] + monoScratch[i]);
                s += m;
            }
        }
        recorder.push (wetL, wetR, n, params.infinite);
        pos += n;
    }

    const auto& st = reverb.getStats();
    meters.input.store (st.inputLevel, std::memory_order_relaxed);
    meters.wet.store (st.wetRms, std::memory_order_relaxed);
    meters.low.store (st.wetLow, std::memory_order_relaxed);
    meters.high.store (st.wetHigh, std::memory_order_relaxed);
}

void HorizonNoirProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HorizonNoirProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HorizonNoirProcessor::createEditor()
{
    return new HorizonNoirEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HorizonNoirProcessor();
}
