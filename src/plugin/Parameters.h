#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
inline constexpr const char* size = "size";
inline constexpr const char* decay = "decay";
inline constexpr const char* tone = "tone";
inline constexpr const char* shimmer = "shimmer";
inline constexpr const char* ducking = "ducking";
inline constexpr const char* mix = "mix";
inline constexpr const char* infinite = "infinite";
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

    auto toneText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
        const int pct = juce::roundToInt (v * 100.0f);
        return pct < 50 ? "Sombre " + String (100 - 2 * pct) + " %" : (pct == 50 ? String ("Neutre") : "Clair " + String (2 * pct - 100) + " %");
    }).withValueFromStringFunction ([] (const String& t) {
        const float amount = t.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
        if (t.containsIgnoreCase ("sombre")) return 0.5f - 0.5f * amount;
        if (t.containsIgnoreCase ("clair")) return 0.5f + 0.5f * amount;
        if (t.containsIgnoreCase ("neutre")) return 0.5f;
        return amount; // plain percentage
    });

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
        const char* name = std::abs (st) == 12 ? "Octave" : (std::abs (st) == 7 ? "Quinte" : "Quarte");
        return st == 0 ? String ("Off") : String (name) + (st > 0 ? " +" : " -");
    }).withValueFromStringFunction ([] (const String& t) {
        if (t.containsIgnoreCase ("off")) return 0.0f;
        const float sign = t.contains ("-") ? -1.0f : 1.0f;
        if (t.containsIgnoreCase ("oct")) return 12.0f * sign;
        if (t.containsIgnoreCase ("quint")) return 7.0f * sign;
        if (t.containsIgnoreCase ("quart")) return 5.0f * sign;
        return t.retainCharacters ("-0123456789.").getFloatValue();
    });

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::size, 1 }, "Taille", NormalisableRange<float> (0.0f, 1.0f), 0.75f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::decay, 1 }, "Duree", decayRange, 20.0f, seconds));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::tone, 1 }, "Sombre/Clair", NormalisableRange<float> (0.0f, 1.0f), 0.3f, toneText));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::shimmer, 1 }, "Shimmer", shimmerRange, 0.0f, semis));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::ducking, 1 }, "Ducking", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::mix, 1 }, "Dry/Wet", NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::infinite, 1 }, "Infini", false));
    return layout;
}
