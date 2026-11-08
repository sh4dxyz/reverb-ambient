#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rvb
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

inline bool isPrime (int n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; d * d <= n; d += 2)
        if (n % d == 0) return false;
    return true;
}

inline int nextPrimeAtLeast (int n)
{
    n = std::max (n, 2);
    while (! isPrime (n)) ++n;
    return n;
}

inline int gcd (int a, int b)
{
    while (b != 0) { const int t = a % b; a = b; b = t; }
    return a;
}

// Flushes denormals to zero without depending on CPU flags (keeps the code portable).
inline float flushDenormal (float x) { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }

// In-place fast Walsh-Hadamard transform, normalised so the matrix is orthonormal
// (energy preserving): H16 / 4.
inline void hadamard16 (float* x)
{
    for (int h = 1; h < 16; h <<= 1)
        for (int i = 0; i < 16; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    for (int i = 0; i < 16; ++i) x[i] *= 0.25f;
}

// Soft limiter: transparent below the threshold, smoothly saturating above it,
// never exceeding `ceiling`.
inline float softLimit (float x, float threshold, float ceiling)
{
    const float a = std::fabs (x);
    if (a <= threshold) return x;
    const float range = ceiling - threshold;
    const float y = threshold + range * std::tanh ((a - threshold) / range);
    return x < 0.0f ? -y : y;
}

// Fractional delay line with a power-of-two circular buffer and 4-point Hermite reads.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x)
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    // Integer read: delay of d samples (d >= 1) relative to the next write.
    float readInt (int d) const { return buffer[(size_t) ((writePos - d) & mask)]; }

    // Fractional read, delay in samples (>= 2).
    float read (float delay) const
    {
        const int di = (int) delay;
        const float f = delay - (float) di;
        const float xm1 = readInt (di - 1);
        const float x0 = readInt (di);
        const float x1 = readInt (di + 1);
        const float x2 = readInt (di + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    int capacity() const { return mask + 1; }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

// Schroeder allpass diffuser (Dattorro style): y = -g*x + d ; d' = x + g*y.
class Allpass
{
public:
    void prepare (int delaySamples, float coefficient)
    {
        delay = std::max (1, delaySamples);
        line.prepare (delay + 1);
        g = coefficient;
    }
    void clear() { line.clear(); }

    float process (float x)
    {
        const float d = line.readInt (delay);
        const float v = x + g * d;
        line.push (flushDenormal (v));
        return d - g * v;
    }

private:
    DelayLine line;
    int delay = 1;
    float g = 0.5f;
};

// RBJ biquad, transposed direct form II.
class Biquad
{
public:
    void setHighPass (double sampleRate, double freq, double q = 0.70710678)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 + cs) * 0.5) / a0);
        b1 = (float) (-(1.0 + cs) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cs) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }
    void clear() { z1 = z2 = 0.0f; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal (b1 * x - a1 * y + z2);
        z2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }

private:
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

class OnePoleLowpass
{
public:
    void setCoefficient (float a) { coeff = a; }
    static float coefficientFor (double sampleRate, double freq)
    {
        return (float) std::exp (-2.0 * 3.14159265358979323846 * freq / sampleRate);
    }
    void clear() { z = 0.0f; }
    float process (float x)
    {
        z = flushDenormal (x + coeff * (z - x));
        return z;
    }

private:
    float coeff = 0.0f, z = 0.0f;
};

// Delay-line granular pitch shifter: two read heads sweeping a window, sin^2 crossfade.
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        window = (float) std::round (sampleRate * 0.060); // 60 ms grains
        line.prepare ((int) window + 8);
        phase = 0.0f;
    }
    void clear() { line.clear(); phase = 0.0f; }
    void setRatio (float r) { ratio = r; }

    float process (float x)
    {
        line.push (x);
        // The read offset moves at (1 - ratio) samples per sample.
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float d1 = 3.0f + phase * window;
        const float d2 = 3.0f + p2 * window;
        const float w1 = std::sin (kPi * phase);
        const float w2 = std::sin (kPi * p2);
        return line.read (d1) * w1 * w1 + line.read (d2) * w2 * w2;
    }

private:
    DelayLine line;
    float window = 2048.0f, phase = 0.0f, ratio = 1.0f;
};
} // namespace rvb
