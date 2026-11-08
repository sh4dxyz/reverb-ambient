#pragma once

// Dark ambient reverb engine, independent from JUCE so it can be unit tested anywhere.
//
//   in L/R -> fixed low cut -> Dattorro style input diffusers (4 allpasses per side)
//          -> 16-line FDN (mutually prime lengths, orthonormal Hadamard matrix, Jot)
//             with slow delay modulation, in-loop Jot absorption filters and in-loop shimmer
//          -> output low cut -> soft limiter -> ducking -> dry/wet

#include "DspUtils.h"

#include <array>

namespace rvb
{
struct Params
{
    float size = 0.7f;          // 0..1
    float decaySeconds = 20.0f; // 1..60
    float tone = 0.35f;         // 0 = dark .. 1 = bright
    float shimmerSemis = 0.0f;  // -12..+12, 0 = off
    float ducking = 0.0f;       // 0..1
    float mix = 0.5f;           // 0..1 (dry/wet)
    bool infinite = false;
};

struct Stats
{
    float inputLevel = 0.0f; // peak envelope of the dry input
    float wetRms = 0.0f;     // RMS of the wet output over the last block
    float wetLow = 0.0f;     // RMS of the wet signal below ~250 Hz
    float wetHigh = 0.0f;    // RMS of the wet signal above ~2.5 kHz
};

class AmbientReverb
{
public:
    static constexpr int numLines = 16;
    static constexpr float minScale = 0.25f;
    static constexpr float maxScale = 2.2f;
    static constexpr float minDecay = 1.0f;
    static constexpr float maxDecay = 60.0f;
    static constexpr float lowCutHz = 100.0f;
    static constexpr float outputLowCutHz = 70.0f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const float fs = (float) sampleRate;

        // Base lengths (ms, at scale 1): geometric spread with a small irregular offset so
        // neighbouring lines never line up.
        static constexpr float jitter[numLines] = { 0.00f, 0.31f, -0.27f, 0.12f, -0.18f, 0.24f, -0.06f, 0.29f,
                                                    -0.22f, 0.08f, -0.30f, 0.17f, -0.11f, 0.26f, -0.15f, 0.05f };
        for (int i = 0; i < numLines; ++i)
        {
            const float t = ((float) i + jitter[i]) / (float) (numLines - 1);
            baseMs[(size_t) i] = 23.0f * std::pow (97.0f / 23.0f, std::clamp (t, 0.0f, 1.0f));
        }

        modDepthMax = 0.0016f * fs;
        const int maxLen = (int) std::ceil (97.0f * 1.02f * maxScale * fs / 1000.0f + modDepthMax + 16.0f);
        for (auto& l : lines) l.prepare (maxLen);

        static constexpr float lfoHz[numLines] = { 0.071f, 0.113f, 0.089f, 0.157f, 0.131f, 0.097f, 0.181f, 0.067f,
                                                   0.149f, 0.079f, 0.173f, 0.103f, 0.127f, 0.193f, 0.083f, 0.139f };
        for (int i = 0; i < numLines; ++i)
        {
            lfoInc[(size_t) i] = lfoHz[i] / fs;
            lfoPhase[(size_t) i] = (float) i / (float) numLines;
        }

        static constexpr int apL[4] = { 142, 107, 379, 277 };
        static constexpr int apR[4] = { 151, 113, 367, 263 };
        static constexpr float apG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
        const double apScale = sampleRate / 29761.0;
        for (int k = 0; k < 4; ++k)
        {
            diffL[(size_t) k].prepare ((int) std::round (apL[k] * apScale), apG[k]);
            diffR[(size_t) k].prepare ((int) std::round (apR[k] * apScale), apG[k]);
        }

        for (auto* f : { &lowCutL, &lowCutR }) f->setHighPass (sampleRate, lowCutHz);
        for (auto* f : { &outCutL, &outCutR }) f->setHighPass (sampleRate, outputLowCutHz);

        shifter.prepare (sampleRate);
        shimHighpass.setHighPass (sampleRate, 120.0);
        shimLowpass.setCoefficient (OnePoleLowpass::coefficientFor (sampleRate, 9000.0));
        bandLowCoeff = OnePoleLowpass::coefficientFor (sampleRate, 250.0);
        bandHighCoeff = OnePoleLowpass::coefficientFor (sampleRate, 2500.0);
        duckAttack = std::exp (-1.0f / (0.005f * fs));
        duckRelease = std::exp (-1.0f / (0.300f * fs));
        // Control-rate one-pole smoothing (~60 ms) and per-sample delay glide (~120 ms).
        ctrlSmooth = std::exp (-(float) controlInterval / (0.060f * fs));
        lengthGlide = 1.0f - std::exp (-1.0f / (0.120f * fs));
        energySmooth = 1.0f - std::exp (-(float) controlInterval / (0.300f * fs));

        prepared = true;
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        absorbState.fill (0.0f);
        for (auto& a : diffL) a.clear();
        for (auto& a : diffR) a.clear();
        lowCutL.clear(); lowCutR.clear(); outCutL.clear(); outCutR.clear();
        shifter.clear();
        shimHighpass.clear();
        shimLowpass.clear();
        duckEnv = bandLowZ = bandHighZ = 0.0f;
        lastShimmer = 0.0f;
        loopEnergyAcc = loopEnergy = 0.0f;
        infTargetEnergy = -1.0f;
        agc = 1.0f;
        snapNext = true;
        countdown = 0;
        stats = {};
    }

    void setParams (const Params& p) { target = p; }
    const Stats& getStats() const { return stats; }
    double getSampleRate() const { return sampleRate; }

    // Current FDN delay lengths in samples (targets, mutually prime).
    const std::array<int, numLines>& getPrimeLengths() const { return primeLen; }

    // Processes n samples. Input and output may alias. wetL/wetR (optional) receive the
    // wet signal before ducking and mixing; this is what the WAV recorder captures.
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  float* wetOutL, float* wetOutR, int n)
    {
        if (! prepared) return;

        float sumSq = 0.0f, lowSq = 0.0f, highSq = 0.0f;
        std::array<float, numLines> y {}, v {};

        for (int s = 0; s < n; ++s)
        {
            if (--countdown <= 0) updateControl();

            const float dryL = inL[s];
            const float dryR = inR != nullptr ? inR[s] : dryL;

            // Ducking envelope follows the dry input.
            const float level = std::max (std::fabs (dryL), std::fabs (dryR));
            const float coeff = level > duckEnv ? duckAttack : duckRelease;
            duckEnv = flushDenormal (level + coeff * (duckEnv - level));

            // Input conditioning: low cut then diffusion.
            float dl = lowCutL.process (dryL) * cur.inputGain;
            float dr = lowCutR.process (dryR) * cur.inputGain;
            for (int k = 0; k < 4; ++k)
            {
                dl = diffL[(size_t) k].process (dl);
                dr = diffR[(size_t) k].process (dr);
            }

            // FDN read with slow modulation.
            for (int i = 0; i < numLines; ++i)
            {
                auto& len = curLen[(size_t) i];
                len += (float) (primeLen[(size_t) i] - len) * lengthGlide;
                float& ph = lfoPhase[(size_t) i];
                ph += lfoInc[(size_t) i];
                if (ph >= 1.0f) ph -= 1.0f;
                const float mod = cur.modDepth * (0.5f + 0.5f * fastSin (ph));
                y[(size_t) i] = lines[(size_t) i].read (len + 2.0f + mod);
            }

            // Stereo output taps (two decorrelated sign patterns).
            float wl = 0.0f, wr = 0.0f, mono = 0.0f;
            for (int i = 0; i < numLines; ++i)
            {
                wl += outSignL[i] * y[(size_t) i];
                wr += outSignR[i] * y[(size_t) i];
                mono += shimSignIn[i] * y[(size_t) i];
            }

            // Shimmer: pitch shift a mono mix of the loop and feed it back in.
            float shim = 0.0f;
            // The shifted signal is band-limited so octave cascades cannot pile up inaudible
            // sub-bass or ultrasonic energy in the loop.
            if (cur.shimmerAmount > 1.0e-4f)
                shim = shimLowpass.process (shimHighpass.process (shifter.process (mono * 0.25f))) * cur.shimmerAmount;
            else
                shimLowpass.process (shimHighpass.process (shifter.process (0.0f)));
            lastShimmer = shim;

            v = y;
            hadamard16 (v.data());
            for (int i = 0; i < numLines; ++i)
            {
                // Absorption (Jot): first-order high shelf, so highs decay faster than lows
                // and the tail darkens as it lasts.
                float& z = absorbState[(size_t) i];
                const float vi = v[(size_t) i];
                z = flushDenormal (vi + cur.shelfCoeff * (z - vi));
                const float r = cur.shelfRatio[(size_t) i];
                float x = cur.gain[(size_t) i] * (r * vi + (1.0f - r) * z)
                        + 0.25f * (injSignL[i] * dl + injSignR[i] * dr)
                        + 0.25f * shimSignOut[i] * shim;
                x = softLimit (x, loopLimitThreshold, loopLimitCeiling);
                loopEnergyAcc += x * x;
                lines[(size_t) i].push (flushDenormal (x));
            }

            // Output conditioning.
            wl = outCutL.process (wl * outputScale) * cur.wetComp;
            wr = outCutR.process (wr * outputScale) * cur.wetComp;
            wl = softLimit (wl, 0.8f, 1.0f);
            wr = softLimit (wr, 0.8f, 1.0f);

            if (wetOutL != nullptr) wetOutL[s] = wl;
            if (wetOutR != nullptr) wetOutR[s] = wr;

            // Meters for the visuals.
            const float wm = 0.5f * (wl + wr);
            sumSq += wm * wm;
            bandLowZ = flushDenormal (wm + bandLowCoeff * (bandLowZ - wm));
            bandHighZ = flushDenormal (wm + bandHighCoeff * (bandHighZ - wm));
            lowSq += bandLowZ * bandLowZ;
            const float hi = wm - bandHighZ;
            highSq += hi * hi;

            const float duck = 1.0f / (1.0f + cur.ducking * 24.0f * duckEnv);
            outL[s] = dryL * cur.dryGain + wl * duck * cur.wetGain;
            outR[s] = dryR * cur.dryGain + wr * duck * cur.wetGain;
        }

        if (n > 0)
        {
            const float invN = 1.0f / (float) n;
            stats.inputLevel = duckEnv;
            stats.wetRms = std::sqrt (sumSq * invN);
            stats.wetLow = std::sqrt (lowSq * invN);
            stats.wetHigh = std::sqrt (highSq * invN);
        }
    }

    // Exposed for tests.
    float getLoopGain (int line) const { return cur.gain[(size_t) line]; }
    float getInfiniteAmount() const { return cur.infinite; }
    float getAgc() const { return agc; }
    float getLoopEnergy() const { return loopEnergy; }

private:
    struct Control
    {
        float scale = 1.0f;
        float logDecay = 0.0f;
        float tone = 0.5f;
        float infinite = 0.0f;
        float shimmerAmount = 0.0f;
        float shimmerSemis = 0.0f;
        float ducking = 0.0f;
        float mix = 0.5f;
        // Derived.
        float inputGain = 1.0f;
        float modDepth = 0.0f;
        float wetComp = 1.0f;
        float dryGain = 1.0f, wetGain = 0.0f;
        std::array<float, numLines> gain {}, shelfRatio {};
        float shelfCoeff = 0.0f;
    };

    static constexpr int controlInterval = 16;
    static constexpr float shimmerMaxAmount = 0.55f;
    static constexpr float loopLimitThreshold = 1.0f;
    static constexpr float loopLimitCeiling = 2.0f;
    static constexpr float outputScale = 0.35f;

    static constexpr float outSignL[numLines] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1 };
    // R = L * m where m holds eight +1 and eight -1: the two taps are orthogonal, so the
    // tail is decorrelated (not anti-correlated) and survives a mono fold-down.
    static constexpr float outSignR[numLines] = { 1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1, -1, -1, 1, -1, -1 };
    static constexpr float injSignL[numLines] = { 1, 1, 1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, 1, -1, 1 };
    static constexpr float injSignR[numLines] = { -1, 1, 1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1, -1, -1, -1 };
    static constexpr float shimSignIn[numLines] = { 1, 1, -1, -1, 1, -1, 1, 1, 1, -1, -1, 1, -1, 1, 1, -1 };
    static constexpr float shimSignOut[numLines] = { 1, -1, -1, 1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, 1 };

    static float fastSin (float phase01)
    {
        // Parabolic sine approximation, plenty for a slow LFO.
        const float x = phase01 * 2.0f - 1.0f; // -1..1
        return -4.0f * x * (1.0f - std::fabs (x));
    }

    static float scaleForSize (float size) { return minScale * std::pow (maxScale / minScale, std::clamp (size, 0.0f, 1.0f)); }

    // Shimmer level fades in over the first semitone away from 0.
    static float shimmerAmountFor (float semis) { return shimmerMaxAmount * std::clamp (std::fabs (semis), 0.0f, 1.0f); }

    void computePrimeLengths (float scale)
    {
        const float fs = (float) sampleRate;
        for (int i = 0; i < numLines; ++i)
        {
            int p = nextPrimeAtLeast ((int) std::round (baseMs[(size_t) i] * scale * fs / 1000.0f));
            for (int j = 0; j < i; ++j)
                if (primeLen[(size_t) j] == p) { p = nextPrimeAtLeast (p + 1); j = -1; }
            primeLen[(size_t) i] = p;
        }
        primeScale = scale;
    }

    void updateControl()
    {
        countdown = controlInterval;
        updateInfiniteLevel();
        const float targetScale = scaleForSize (target.size);
        const float targetLogDecay = std::log (std::clamp (target.decaySeconds, minDecay, maxDecay));
        const float targetInf = target.infinite ? 1.0f : 0.0f;
        const float semis = std::round (std::clamp (target.shimmerSemis, -12.0f, 12.0f));

        if (snapNext)
        {
            cur.scale = targetScale;
            cur.logDecay = targetLogDecay;
            cur.tone = target.tone;
            cur.infinite = targetInf;
            cur.shimmerSemis = semis;
            cur.shimmerAmount = shimmerAmountFor (semis);
            cur.ducking = target.ducking;
            cur.mix = target.mix;
            computePrimeLengths (targetScale);
            for (int i = 0; i < numLines; ++i) curLen[(size_t) i] = (float) primeLen[(size_t) i];
            snapNext = false;
        }
        else
        {
            const float a = ctrlSmooth;
            auto smooth = [a] (float& c, float t) { c = t + a * (c - t); };
            smooth (cur.scale, targetScale);
            smooth (cur.logDecay, targetLogDecay);
            smooth (cur.tone, target.tone);
            // INFINI engages over ~0.25 s so the freeze never clicks.
            const float infStep = (float) controlInterval / (0.25f * (float) sampleRate);
            cur.infinite += std::clamp (targetInf - cur.infinite, -infStep, infStep);
            // Pitch changes glide; the amount fades through 0 when the sign flips.
            const bool flipping = (semis * cur.shimmerSemis) < 0.0f;
            const float shimTarget = flipping ? 0.0f : shimmerAmountFor (semis);
            smooth (cur.shimmerAmount, shimTarget);
            if (! flipping || cur.shimmerAmount < 0.01f) smooth (cur.shimmerSemis, semis);
            smooth (cur.ducking, target.ducking);
            smooth (cur.mix, target.mix);

            if (std::fabs (cur.scale - primeScale) > 0.002f * primeScale)
                computePrimeLengths (cur.scale);
        }

        const float fs = (float) sampleRate;
        const float decay = std::exp (cur.logDecay);
        // The shimmer path feeds about amount^2 / 16 of the loop energy back in on every pass
        // (times 3/4: the two sin^2-windowed grains of the pitch shifter keep 75 % of the energy):
        // take the same energy out of the recirculation so decay times stay honest (and
        // INFINI + shimmer neither dies nor explodes).
        const float shimEnergy = 0.75f * cur.shimmerAmount * cur.shimmerAmount / (float) numLines;
        const float tone = std::clamp (cur.tone, 0.0f, 1.0f);
        // Sombre/Clair: shelf corner frequency and high frequency decay time (as a fraction
        // of the main decay).
        const float shelfHz = 700.0f * std::pow (7000.0f / 700.0f, tone);
        const float hfRatio = 0.07f * std::pow (0.7f / 0.07f, tone);
        // In INFINI the lows are held forever while the highs still fade very slowly.
        const float infiniteHfDecay = 30.0f + 90.0f * tone;
        cur.shelfCoeff = OnePoleLowpass::coefficientFor (sampleRate, shelfHz);
        for (int i = 0; i < numLines; ++i)
        {
            const float lenSec = curLen[(size_t) i] / fs;
            float gLow = std::pow (10.0f, -3.0f * lenSec / decay);
            float gHigh = std::pow (10.0f, -3.0f * lenSec / (decay * hfRatio));
            const float gHighInf = std::pow (10.0f, -3.0f * lenSec / infiniteHfDecay);
            gLow += (1.0f - gLow) * cur.infinite;
            gHigh += (gHighInf - gHigh) * cur.infinite;
            const float trimmed = std::sqrt (std::max (gLow * gLow - shimEnergy, 0.0f));
            cur.gain[(size_t) i] = trimmed * (1.0f + (agc - 1.0f) * cur.infinite);
            cur.shelfRatio[(size_t) i] = gHigh / gLow;
        }
        cur.inputGain = 1.0f - cur.infinite;

        shifter.setRatio (std::pow (2.0f, cur.shimmerSemis / 12.0f));
        cur.shimmerAmount = std::max (cur.shimmerAmount, 0.0f);

        cur.modDepth = modDepthMax * (0.35f + 0.65f * (cur.scale - minScale) / (maxScale - minScale));
        // Long decays pile up energy; compensate (partially) so the level stays usable.
        cur.wetComp = 1.0f / std::sqrt (1.0f + decay / 6.0f);
        cur.dryGain = std::cos (0.5f * kPi * std::clamp (cur.mix, 0.0f, 1.0f));
        cur.wetGain = std::sin (0.5f * kPi * std::clamp (cur.mix, 0.0f, 1.0f));
        if (cur.mix <= 0.0f) { cur.dryGain = 1.0f; cur.wetGain = 0.0f; }
    }

    // INFINI level keeper: once frozen, a slow integrator nudges the loop gain (within
    // +/-1 %) so the loop energy stays at the level captured when INFINI engaged, whatever
    // the shimmer cascades or high frequency absorption take away.
    void updateInfiniteLevel()
    {
        loopEnergy += (loopEnergyAcc / (float) controlInterval - loopEnergy) * energySmooth;
        loopEnergyAcc = 0.0f;

        if (cur.infinite >= 1.0f)
        {
            if (infTargetEnergy < 0.0f) infTargetEnergy = loopEnergy;
            if (infTargetEnergy > 1.0e-12f && loopEnergy > 1.0e-14f)
            {
                const float err = std::clamp (std::log (infTargetEnergy / loopEnergy), -1.0f, 1.0f);
                agc = std::clamp (agc * (1.0f + 0.00001f * err), 0.99f, 1.01f);
            }
        }
        else
        {
            infTargetEnergy = -1.0f;
            agc += (1.0f - agc) * 0.01f;
        }
    }

    double sampleRate = 48000.0;
    bool prepared = false;
    bool snapNext = true;
    int countdown = 0;
    Params target;
    Control cur;
    Stats stats;

    std::array<DelayLine, numLines> lines;
    std::array<float, numLines> absorbState {};
    std::array<float, numLines> baseMs {}, curLen {}, lfoPhase {}, lfoInc {};
    std::array<int, numLines> primeLen {};
    float primeScale = 0.0f;
    float modDepthMax = 0.0f;

    std::array<Allpass, 4> diffL, diffR;
    Biquad lowCutL, lowCutR, outCutL, outCutR;
    PitchShifter shifter;
    Biquad shimHighpass;
    OnePoleLowpass shimLowpass;
    float lastShimmer = 0.0f;

    float duckEnv = 0.0f, duckAttack = 0.0f, duckRelease = 0.0f;
    float bandLowZ = 0.0f, bandHighZ = 0.0f, bandLowCoeff = 0.0f, bandHighCoeff = 0.0f;
    float ctrlSmooth = 0.0f, lengthGlide = 0.0f, energySmooth = 0.0f;
    float loopEnergyAcc = 0.0f, loopEnergy = 0.0f, infTargetEnergy = -1.0f, agc = 1.0f;
};
} // namespace rvb
