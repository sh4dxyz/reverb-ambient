#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
inline constexpr const char* size = "size";
inline constexpr const char* decay = "decay";
inline constexpr const char* damping = "damping";
inline constexpr const char* shimmer = "shimmer";
inline constexpr const char* ducking = "ducking";
inline constexpr const char* mix = "mix";
inline constexpr const char* freeze = "freeze";
} // namespace ParamIDs

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using Float = AudioParameterFloat;

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("-0123456789.").getFloatValue() / 100.0f; });

    NormalisableRange<float> decayRange (1.0f, 60.0f, 0.01f);
    decayRange.setSkewForCentre (12.0f);

    auto seconds = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return v < 10.0f ? String (v, 1) + " s" : String (juce::roundToInt (v)) + " s"; })
        .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); });

    // Shimmer: 7 detents (octave / fifth / fourth down, off, fourth / fifth / octave up).
    static constexpr float intervals[] = { -12.0f, -7.0f, -5.0f, 0.0f, 5.0f, 7.0f, 12.0f };
    auto toIndex = [] (float st) {
        int best = 3;
        for (int i = 0; i < 7; ++i)
            if (std::abs (intervals[i] - st) < std::abs (intervals[best] - st)) best = i;
        return best;
    };
    NormalisableRange<float> shimmerRange (
        -12.0f, 12.0f,
        [] (float, float, float x) { return intervals[juce::jlimit (0, 6, juce::roundToInt (x * 6.0f))]; },
        [toIndex] (float, float, float v) { return (float) toIndex (v) / 6.0f; },
        [toIndex] (float, float, float v) { return intervals[toIndex (v)]; });

    auto semis = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
        const int st = juce::roundToInt (v);
        return st == 0 ? String ("Off") : (st > 0 ? "+" : "") + String (st) + " st";
    }).withValueFromStringFunction ([] (const String& t) {
        if (t.containsIgnoreCase ("off")) return 0.0f;
        return t.retainCharacters ("-0123456789.").getFloatValue();
    });

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::size, 1 }, "Size", NormalisableRange<float> (0.0f, 1.0f), 0.75f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::decay, 1 }, "Decay", decayRange, 20.0f, seconds));
    // Damping: how fast the highs die in the tail (100 % = darkest).
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::damping, 1 }, "Damping", NormalisableRange<float> (0.0f, 1.0f), 0.7f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::shimmer, 1 }, "Shimmer", shimmerRange, 0.0f, semis));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::ducking, 1 }, "Ducking", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));
    return layout;
}
