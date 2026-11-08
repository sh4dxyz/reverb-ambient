#pragma once

// Dark ambient reverb engine, independent from JUCE so it can be unit tested anywhere.
//
//   in L/R -> fixed low cut -> Dattorro style input diffusers (4 allpasses per side)
//          -> two 16-line FDN tanks (mutually prime lengths, orthonormal Hadamard matrix,
//             Jot absorption, slow modulation, in-loop shimmer)
//          -> output low cut -> soft limiter -> ducking -> dry/wet
//
// Why two tanks: changing the delay lengths of a tail that is already ringing always bends
// or smears its pitch. When Taille changes, new sound is sent to the idle tank at the new
// size while the previous tail finishes in the other tank, untouched.

#include "DspUtils.h"

#include <array>

namespace rvb
{
struct Params
{
    float size = 0.7f;          // 0..1
    float decaySeconds = 20.0f; // 1..60
    float tone = 0.35f;         // 0 = dark .. 1 = bright
    float shimmerSemis = 0.0f;  // -12..+12 (snapped to octave / fifth / fourth), 0 = off
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

        modDepthMax = 0.0006f * fs;
        const int maxLen = (int) std::ceil (97.0f * 1.02f * maxScale * fs / 1000.0f + modDepthMax + 16.0f);

        static constexpr float lfoHz[numLines] = { 0.071f, 0.113f, 0.089f, 0.157f, 0.131f, 0.097f, 0.181f, 0.067f,
                                                   0.149f, 0.079f, 0.173f, 0.103f, 0.127f, 0.193f, 0.083f, 0.139f };
        for (auto& tank : tanks)
        {
            for (auto& l : tank.lines) l.prepare (maxLen);
            for (int i = 0; i < numLines; ++i)
            {
                tank.lfoInc[(size_t) i] = lfoHz[i] / fs;
                tank.lfoPhase[(size_t) i] = (float) i / (float) numLines;
            }
            tank.shifter.prepare (sampleRate);
            tank.shimHighpass.setHighPass (sampleRate, 120.0);
            tank.shimLowpass.setCoefficient (OnePoleLowpass::coefficientFor (sampleRate, 9000.0));
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

        bandLowCoeff = OnePoleLowpass::coefficientFor (sampleRate, 250.0);
        bandHighCoeff = OnePoleLowpass::coefficientFor (sampleRate, 2500.0);
        duckAttack = std::exp (-1.0f / (0.005f * fs));
        duckRelease = std::exp (-1.0f / (0.300f * fs));
        ctrlSmooth = std::exp (-(float) controlInterval / (0.060f * fs));
        // Tank hand-over: input crossfade, and fade-out of a tail that has to be recycled.
        feedStep = (float) controlInterval / (0.040f * fs);
        killStep = (float) controlInterval / (0.120f * fs);
        settleControls = (int) (0.150f * fs / (float) controlInterval);

        prepared = true;
        reset();
    }

    void reset()
    {
        for (auto& tank : tanks) tank.clear();
        for (auto& a : diffL) a.clear();
        for (auto& a : diffR) a.clear();
        lowCutL.clear(); lowCutR.clear(); outCutL.clear(); outCutR.clear();
        duckEnv = bandLowZ = bandHighZ = 0.0f;
        snapNext = true;
        countdown = 0;
        stats = {};
    }

    void setParams (const Params& p) { target = p; }
    const Stats& getStats() const { return stats; }
    double getSampleRate() const { return sampleRate; }

    // Delay lengths (samples, mutually prime) of the tank receiving new sound.
    const std::array<int, numLines>& getPrimeLengths() const { return tanks[(size_t) active].len; }

    // Processes n samples. Input and output may alias. wetL/wetR (optional) receive the
    // wet signal before ducking and mixing; this is what the WAV recorder captures.
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  float* wetOutL, float* wetOutR, int n)
    {
        if (! prepared) return;

        float sumSq = 0.0f, lowSq = 0.0f, highSq = 0.0f;

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

            float wl = 0.0f, wr = 0.0f;
            for (auto& tank : tanks)
                if (tank.state != TankState::idle)
                    tank.process (dl, dr, cur, wl, wr);

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
    float getLoopGain (int line) const { return tanks[(size_t) active].gain[(size_t) line]; }
    float getInfiniteAmount() const { return cur.infinite; }
    int getActiveTank() const { return active; }

    // The Shimmer knob only lands on consonant intervals: octave, fifth, fourth (up or down).
    static float musicalInterval (float semis)
    {
        static constexpr float steps[] = { -12.0f, -7.0f, -5.0f, 0.0f, 5.0f, 7.0f, 12.0f };
        float best = 0.0f;
        for (float st : steps)
            if (std::fabs (st - semis) < std::fabs (best - semis)) best = st;
        return best;
    }

private:
    // Values shared by both tanks, updated at control rate.
    struct Control
    {
        float logDecay = 0.0f;
        float tone = 0.5f;
        float infinite = 0.0f;
        float shimmerAmount = 0.0f;
        float shimmerSemis = 0.0f;
        float ducking = 0.0f;
        float mix = 0.5f;
        // Derived.
        float inputGain = 1.0f;
        float shimmerLive = 0.0f;
        float shelfCoeff = 0.0f;
        float wetComp = 1.0f;
        float dryGain = 1.0f, wetGain = 0.0f;
    };

    static constexpr int controlInterval = 16;
    static constexpr float shimmerMaxAmount = 0.35f;
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

    enum class TankState { idle, feeding, releasing, killing };

    struct Tank
    {
        std::array<DelayLine, numLines> lines;
        std::array<float, numLines> absorbState {}, lfoPhase {}, lfoInc {}, gain {}, shelfRatio {};
        std::array<int, numLines> len {};
        PitchShifter shifter;
        Biquad shimHighpass;
        OnePoleLowpass shimLowpass;
        TankState state = TankState::idle;
        float scale = 1.0f;
        float modDepth = 0.0f;
        float feed = 0.0f;  // 0..1, how much new input this tank receives
        float level = 1.0f; // output fade (used when a tail is recycled)

        void clear()
        {
            for (auto& l : lines) l.clear();
            absorbState.fill (0.0f);
            shifter.clear();
            shimHighpass.clear();
            shimLowpass.clear();
            state = TankState::idle;
            feed = 0.0f;
            level = 1.0f;
        }

        void process (float dl, float dr, const Control& c, float& outL, float& outR)
        {
            std::array<float, numLines> y {}, v {};
            for (int i = 0; i < numLines; ++i)
            {
                const auto li = (size_t) i;
                float& ph = lfoPhase[li];
                ph += lfoInc[li];
                if (ph >= 1.0f) ph -= 1.0f;
                y[li] = lines[li].read ((float) len[li] + 2.0f + modDepth * (0.5f + 0.5f * fastSin (ph)));
            }

            float wl = 0.0f, wr = 0.0f, mono = 0.0f;
            for (int i = 0; i < numLines; ++i)
            {
                wl += outSignL[i] * y[(size_t) i];
                wr += outSignR[i] * y[(size_t) i];
                mono += shimSignIn[i] * y[(size_t) i];
            }

            // Shimmer: pitch shift a mono mix of the loop and feed it back in. The shifted
            // signal is band-limited so octave cascades cannot pile up sub-bass or
            // ultrasonic energy in the loop.
            float shim = shimLowpass.process (shimHighpass.process (shifter.process (mono * 0.25f)));
            shim *= c.shimmerLive;

            v = y;
            hadamard16 (v.data());
            const float inL = 0.25f * dl * feed, inR = 0.25f * dr * feed;
            for (int i = 0; i < numLines; ++i)
            {
                // Absorption (Jot): first-order high shelf, so highs decay faster than lows
                // and the tail darkens as it lasts.
                const auto li = (size_t) i;
                float& z = absorbState[li];
                const float vi = v[li];
                z = flushDenormal (vi + c.shelfCoeff * (z - vi));
                const float r = shelfRatio[li];
                float x = gain[li] * (r * vi + (1.0f - r) * z)
                        + injSignL[i] * inL + injSignR[i] * inR
                        + 0.25f * shimSignOut[i] * shim;
                x = softLimit (x, loopLimitThreshold, loopLimitCeiling);
                lines[li].push (flushDenormal (x));
            }

            outL += wl * level;
            outR += wr * level;
        }
    };

    static float scaleForSize (float size) { return minScale * std::pow (maxScale / minScale, std::clamp (size, 0.0f, 1.0f)); }

    // Shimmer level fades in over the first semitone away from 0.
    static float shimmerAmountFor (float semis) { return shimmerMaxAmount * std::clamp (std::fabs (semis), 0.0f, 1.0f); }

    void tuneTank (Tank& tank, float scale)
    {
        const float fs = (float) sampleRate;
        for (int i = 0; i < numLines; ++i)
        {
            int p = nextPrimeAtLeast ((int) std::round (baseMs[(size_t) i] * scale * fs / 1000.0f));
            for (int j = 0; j < i; ++j)
                if (tank.len[(size_t) j] == p) { p = nextPrimeAtLeast (p + 1); j = -1; }
            tank.len[(size_t) i] = p;
        }
        tank.scale = scale;
    }

    // Size changes: once the knob has rested for a moment, route new sound to the other tank.
    void updateTankRouting (float targetScale)
    {
        auto& act = tanks[(size_t) active];
        auto& other = tanks[(size_t) (1 - active)];

        if (std::fabs (targetScale - lastTargetScale) > 0.002f * targetScale)
        {
            lastTargetScale = targetScale;
            stableControls = 0;
        }
        else if (stableControls < settleControls)
        {
            ++stableControls;
        }

        const bool wantsNewSize = std::fabs (std::log (targetScale / act.scale)) > 0.01f;
        // Never re-route while frozen: INFINI keeps exactly what it holds.
        if (wantsNewSize && stableControls >= settleControls && cur.infinite <= 0.0f && ! switching)
        {
            switching = true;
            if (other.state != TankState::idle)
                other.state = TankState::killing; // an older tail must make room first
        }

        if (switching)
        {
            if (other.state == TankState::killing)
            {
                other.level -= killStep;
                if (other.level <= 0.0f) other.clear();
            }
            if (other.state == TankState::idle)
            {
                tuneTank (other, lastTargetScale);
                other.state = TankState::feeding;
                other.feed = 0.0f;
                act.state = TankState::releasing;
                active = 1 - active;
                switching = false;
            }
        }

        // Input crossfade between the tanks.
        for (auto& tank : tanks)
        {
            if (tank.state == TankState::feeding) tank.feed = std::min (1.0f, tank.feed + feedStep);
            else tank.feed = std::max (0.0f, tank.feed - feedStep);
        }
    }

    void updateTankGains (Tank& tank, float decay, float hfRatio, float shimEnergy)
    {
        const float fs = (float) sampleRate;
        for (int i = 0; i < numLines; ++i)
        {
            const float lenSec = (float) tank.len[(size_t) i] / fs;
            float gLow = std::pow (10.0f, -3.0f * lenSec / decay);
            float gHigh = std::pow (10.0f, -3.0f * lenSec / (decay * hfRatio));
            gLow += (1.0f - gLow) * cur.infinite;
            gHigh += (1.0f - gHigh) * cur.infinite;
            tank.gain[(size_t) i] = std::sqrt (std::max (gLow * gLow - shimEnergy, 0.0f));
            tank.shelfRatio[(size_t) i] = gHigh / gLow;
        }
        // Modulation accumulates on every pass round the loop; in INFINI it is almost
        // stopped so the frozen pad does not drift into a wobbling chorus.
        tank.modDepth = modDepthMax * (0.35f + 0.65f * (tank.scale - minScale) / (maxScale - minScale))
                      * (1.0f - 0.9f * cur.infinite);
    }

    void updateControl()
    {
        countdown = controlInterval;
        const float targetScale = scaleForSize (target.size);
        const float targetLogDecay = std::log (std::clamp (target.decaySeconds, minDecay, maxDecay));
        const float targetInf = target.infinite ? 1.0f : 0.0f;
        const float semis = musicalInterval (target.shimmerSemis);

        if (snapNext)
        {
            cur.logDecay = targetLogDecay;
            cur.tone = target.tone;
            cur.infinite = targetInf;
            cur.shimmerSemis = semis;
            cur.shimmerAmount = shimmerAmountFor (semis);
            cur.ducking = target.ducking;
            cur.mix = target.mix;
            for (auto& tank : tanks) tank.clear();
            active = 0;
            switching = false;
            tuneTank (tanks[0], targetScale);
            tuneTank (tanks[1], targetScale);
            tanks[0].state = TankState::feeding;
            tanks[0].feed = 1.0f;
            lastTargetScale = targetScale;
            stableControls = settleControls;
            snapNext = false;
        }
        else
        {
            const float a = ctrlSmooth;
            auto smooth = [a] (float& c, float t) { c = t + a * (c - t); };
            smooth (cur.logDecay, targetLogDecay);
            smooth (cur.tone, target.tone);
            // INFINI engages over ~0.25 s so the freeze never clicks.
            const float infStep = (float) controlInterval / (0.25f * (float) sampleRate);
            cur.infinite += std::clamp (targetInf - cur.infinite, -infStep, infStep);
            // Pitch changes glide; the amount fades through 0 when the interval changes.
            const bool changing = std::fabs (semis - cur.shimmerSemis) > 0.01f;
            smooth (cur.shimmerAmount, changing ? 0.0f : shimmerAmountFor (semis));
            if (changing && cur.shimmerAmount < 0.01f) cur.shimmerSemis = semis;
            smooth (cur.ducking, target.ducking);
            smooth (cur.mix, target.mix);
        }

        updateTankRouting (targetScale);

        const float decay = std::exp (cur.logDecay);
        // INFINI is a true freeze: loop gain exactly 1, no absorption, no new input and no
        // shimmer, so the held pad keeps the timbre of what was playing.
        cur.shimmerLive = cur.shimmerAmount * (1.0f - cur.infinite);
        // The shimmer path feeds about amount^2 / 16 of the loop energy back in on every pass
        // (times 3/4: the two sin^2-windowed grains keep 75 % of the energy): take the same
        // energy out of the recirculation so decay times stay honest.
        const float shimEnergy = 0.75f * cur.shimmerLive * cur.shimmerLive / (float) numLines;
        const float tone = std::clamp (cur.tone, 0.0f, 1.0f);
        // Sombre/Clair: shelf corner frequency and high frequency decay time (as a fraction
        // of the main decay).
        const float shelfHz = 700.0f * std::pow (7000.0f / 700.0f, tone);
        const float hfRatio = 0.07f * std::pow (0.7f / 0.07f, tone);
        cur.shelfCoeff = OnePoleLowpass::coefficientFor (sampleRate, shelfHz);
        for (auto& tank : tanks)
        {
            if (tank.state == TankState::idle) continue;
            updateTankGains (tank, decay, hfRatio, shimEnergy);
            tank.shifter.setRatio (std::pow (2.0f, cur.shimmerSemis / 12.0f));
        }
        cur.inputGain = 1.0f - cur.infinite;

        // Long decays pile up energy; compensate (partially) so the level stays usable.
        cur.wetComp = 1.0f / std::sqrt (1.0f + decay / 6.0f);
        cur.dryGain = std::cos (0.5f * kPi * std::clamp (cur.mix, 0.0f, 1.0f));
        cur.wetGain = std::sin (0.5f * kPi * std::clamp (cur.mix, 0.0f, 1.0f));
        if (cur.mix <= 0.0f) { cur.dryGain = 1.0f; cur.wetGain = 0.0f; }
    }

    double sampleRate = 48000.0;
    bool prepared = false;
    bool snapNext = true;
    int countdown = 0;
    Params target;
    Control cur;
    Stats stats;

    std::array<Tank, 2> tanks;
    int active = 0;
    bool switching = false;
    float lastTargetScale = 1.0f;
    int stableControls = 0, settleControls = 1;
    float feedStep = 0.01f, killStep = 0.01f;

    std::array<float, numLines> baseMs {};
    float modDepthMax = 0.0f;

    std::array<Allpass, 4> diffL, diffR;
    Biquad lowCutL, lowCutR, outCutL, outCutR;

    float duckEnv = 0.0f, duckAttack = 0.0f, duckRelease = 0.0f;
    float bandLowZ = 0.0f, bandHighZ = 0.0f, bandLowCoeff = 0.0f, bandHighCoeff = 0.0f;
    float ctrlSmooth = 0.0f;
};
} // namespace rvb
