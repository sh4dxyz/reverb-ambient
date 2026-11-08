// Unit tests for the reverb engine and the tail recorder (no JUCE dependency).
// Build: c++ -std=c++17 -O2 -I src tests/DspTests.cpp -o dsp_tests

#include "dsp/AmbientReverb.h"
#include "dsp/TailRecorder.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>

using namespace rvb;

namespace
{
int failures = 0;
int checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("    FAIL: %s\n", what.c_str()); }
}

constexpr double fs = 48000.0;

struct Render
{
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
};

// Renders `seconds` of output for a given input generator, processing in `block` chunks.
Render render (AmbientReverb& rv, double seconds, const std::function<float (size_t, int)>& input,
               int block = 512, bool wetOnly = true)
{
    const size_t n = (size_t) (seconds * fs);
    Render out;
    out.l.resize (n);
    out.r.resize (n);
    std::vector<float> inL ((size_t) block), inR ((size_t) block), oL ((size_t) block), oR ((size_t) block),
        wL ((size_t) block), wR ((size_t) block);
    for (size_t pos = 0; pos < n; pos += (size_t) block)
    {
        const int len = (int) std::min ((size_t) block, n - pos);
        for (int i = 0; i < len; ++i)
        {
            inL[(size_t) i] = input (pos + (size_t) i, 0);
            inR[(size_t) i] = input (pos + (size_t) i, 1);
        }
        rv.process (inL.data(), inR.data(), oL.data(), oR.data(), wL.data(), wR.data(), len);
        for (int i = 0; i < len; ++i)
        {
            out.l[pos + (size_t) i] = wetOnly ? wL[(size_t) i] : oL[(size_t) i];
            out.r[pos + (size_t) i] = wetOnly ? wR[(size_t) i] : oR[(size_t) i];
        }
    }
    return out;
}

double rms (const Render& x, double t0, double t1)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    double s = 0.0;
    for (size_t i = a; i < b; ++i) s += 0.5 * ((double) x.l[i] * x.l[i] + (double) x.r[i] * x.r[i]);
    return std::sqrt (s / (double) std::max<size_t> (1, b - a));
}

double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

bool allFinite (const Render& x, float limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! std::isfinite (x.l[i]) || ! std::isfinite (x.r[i]) || std::fabs (x.l[i]) > limit || std::fabs (x.r[i]) > limit)
            return false;
    return true;
}

// Fraction of energy above `hz` in a window (single-pole split, good enough for trends).
double brightness (const Render& x, double t0, double t1, double hz = 2000.0)
{
    // Two cascaded one-pole high-passes (12 dB/oct).
    const float a = OnePoleLowpass::coefficientFor (fs, hz);
    float z = 0.0f, z2 = 0.0f;
    double hi = 0.0, all = 0.0;
    const size_t s0 = (size_t) (t0 * fs), s1 = std::min (x.size(), (size_t) (t1 * fs));
    for (size_t i = (s0 > 4800 ? s0 - 4800 : 0); i < s1; ++i)
    {
        const float m = 0.5f * (x.l[i] + x.r[i]);
        z = m + a * (z - m);
        const float h1 = m - z;
        z2 = h1 + a * (z2 - h1);
        const float h2 = h1 - z2;
        if (i >= s0) { hi += (double) h2 * h2; all += (double) m * m; }
    }
    return hi / std::max (all, 1.0e-30);
}

// Goertzel magnitude of a frequency in a window of the mono mix.
double toneMagnitude (const Render& x, double t0, double t1, double hz)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    const double w = 2.0 * 3.14159265358979323846 * hz / fs, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) (i - a) / (double) (b - a));
        const double s = 0.5 * (x.l[i] + x.r[i]) * win + c * s1 - s2;
        s2 = s1; s1 = s;
    }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) (b - a);
}

// Estimates T60 from the Schroeder backward integral (fit between -5 and -25 dB).
double estimateT60 (const Render& x)
{
    std::vector<double> e (x.size());
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;)
    {
        acc += 0.5 * ((double) x.l[i] * x.l[i] + (double) x.r[i] * x.r[i]);
        e[i] = acc;
    }
    const double total = e[0];
    double t5 = -1, t25 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double d = 10.0 * std::log10 (e[i] / total);
        if (t5 < 0 && d <= -5.0) t5 = (double) i / fs;
        if (t25 < 0 && d <= -25.0) { t25 = (double) i / fs; break; }
    }
    if (t5 < 0 || t25 < 0) return -1.0;
    return (t25 - t5) * 3.0;
}

std::function<float (size_t, int)> impulse()
{
    return [] (size_t i, int) { return i == 0 ? 1.0f : 0.0f; };
}

std::function<float (size_t, int)> noiseBurst (double seconds, float amp = 0.5f, uint32_t seed = 1)
{
    auto rng = std::make_shared<std::mt19937> (seed);
    auto last = std::make_shared<std::pair<size_t, float>> (SIZE_MAX, 0.0f);
    return [=] (size_t i, int ch) {
        if ((double) i >= seconds * fs) return 0.0f;
        std::uniform_real_distribution<float> d (-amp, amp);
        if (ch == 0 || last->first != i) { last->first = i; last->second = d (*rng); }
        return ch == 0 ? last->second : last->second * 0.8f;
    };
}

std::function<float (size_t, int)> sineBurst (double hz, double seconds, float amp = 0.5f)
{
    return [=] (size_t i, int) {
        if ((double) i >= seconds * fs) return 0.0f;
        const double t = (double) i / fs;
        const double env = std::min (1.0, std::min (t / 0.01, (seconds - t) / 0.01));
        return (float) (amp * env * std::sin (2.0 * 3.14159265358979323846 * hz * t));
    };
}

Params baseParams()
{
    Params p;
    p.size = 0.6f;
    p.decaySeconds = 4.0f;
    p.tone = 0.5f;
    p.shimmerSemis = 0.0f;
    p.ducking = 0.0f;
    p.mix = 1.0f;
    p.infinite = false;
    return p;
}

std::unique_ptr<AmbientReverb> makeReverb (const Params& p, double rate = fs)
{
    auto rv = std::make_unique<AmbientReverb>();
    rv->prepare (rate);
    rv->setParams (p);
    return rv;
}

// ---------------------------------------------------------------------------------------

void testHadamardOrthonormal()
{
    std::mt19937 rng (3);
    std::normal_distribution<float> d;
    for (int t = 0; t < 20; ++t)
    {
        float x[16];
        double e0 = 0, e1 = 0;
        for (auto& v : x) { v = d (rng); e0 += v * v; }
        hadamard16 (x);
        for (auto v : x) e1 += v * v;
        check (std::fabs (e0 - e1) < 1e-3 * e0, "Hadamard preserves energy");
        hadamard16 (x); // H is its own inverse once normalised
    }
    float y[16] = { 1 };
    hadamard16 (y);
    hadamard16 (y);
    check (std::fabs (y[0] - 1.0f) < 1e-6f && std::fabs (y[5]) < 1e-6f, "Hadamard is an involution");
}

void testPrimeLengths()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (float size : { 0.0f, 0.37f, 1.0f })
        {
            auto p = baseParams();
            p.size = size;
            auto rv = makeReverb (p, rate);
            float in = 0, o1, o2;
            rv->process (&in, &in, &o1, &o2, nullptr, nullptr, 1);
            const auto& len = rv->getPrimeLengths();
            bool prime = true, coprime = true;
            for (int i = 0; i < 16; ++i)
            {
                prime &= isPrime (len[(size_t) i]);
                for (int j = i + 1; j < 16; ++j) coprime &= gcd (len[(size_t) i], len[(size_t) j]) == 1;
            }
            check (prime, "all delay lengths are prime (sr " + std::to_string ((int) rate) + ")");
            check (coprime, "delay lengths are mutually prime (sr " + std::to_string ((int) rate) + ")");
        }
}

void testDecayTimes()
{
    for (float target : { 2.0f, 6.0f, 15.0f })
    {
        auto p = baseParams();
        p.decaySeconds = target;
        p.tone = 1.0f; // brightest: closest to a flat decay
        auto rv = makeReverb (p);
        const auto out = render (*rv, target * 1.6 + 1.0, impulse());
        const double t60 = estimateT60 (out);
        std::printf ("    decay %.0f s -> measured T60 %.2f s\n", target, t60);
        check (t60 > target * 0.6 && t60 < target * 1.35, "T60 tracks the Duree setting (" + std::to_string ((int) target) + " s)");
    }
}

void testTailDarkens()
{
    auto p = baseParams();
    p.decaySeconds = 12.0f;
    p.tone = 0.3f;
    auto rv = makeReverb (p);
    const auto out = render (*rv, 8.0, noiseBurst (0.3));
    const double early = brightness (out, 0.4, 1.2), late = brightness (out, 5.0, 7.0);
    std::printf ("    high band share: early %.3f, late %.4f\n", early, late);
    check (late < early * 0.5, "the tail gets darker over time");

    // Sombre vs Clair.
    p.tone = 0.0f;
    auto dark = makeReverb (p);
    p.tone = 1.0f;
    auto bright = makeReverb (p);
    const auto od = render (*dark, 4.0, noiseBurst (0.3)), ob = render (*bright, 4.0, noiseBurst (0.3));
    const double bd = brightness (od, 1.5, 3.5), bb = brightness (ob, 1.5, 3.5);
    std::printf ("    Sombre %.4f vs Clair %.4f\n", bd, bb);
    check (bb > bd * 4.0, "Clair is much brighter than Sombre");
}

void testLowCut()
{
    auto p = baseParams();
    auto rvLow = makeReverb (p), rvMid = makeReverb (p);
    const auto low = render (*rvLow, 3.0, sineBurst (30.0, 2.0)), mid = render (*rvMid, 3.0, sineBurst (500.0, 2.0));
    const double diff = db (rms (mid, 0.5, 2.5)) - db (rms (low, 0.5, 2.5));
    std::printf ("    30 Hz is %.1f dB below 500 Hz\n", diff);
    check (diff > 18.0, "fixed low cut removes rumble");
}

void testStability()
{
    struct Case { float size, decay, tone, shim; bool inf; };
    const Case cases[] = { { 1, 60, 1, 12, false }, { 0, 60, 0, -12, false }, { 1, 60, 1, 12, true },
                           { 1, 1, 0.5f, 7, true }, { 0.5f, 30, 0, -5, true }, { 1, 60, 0, 0, true } };
    for (const auto& c : cases)
    {
        Params p = baseParams();
        p.size = c.size; p.decaySeconds = c.decay; p.tone = c.tone; p.shimmerSemis = c.shim; p.infinite = c.inf;
        auto rv = makeReverb (p);
        // Very hot input (+6 dBFS noise) to stress the limiters.
        const auto out = render (*rv, 20.0, noiseBurst (4.0, 2.0f), 333, false);
        check (allFinite (out, 2.5f), "bounded and finite under stress (size " + std::to_string (c.size) + ", shimmer "
                                          + std::to_string ((int) c.shim) + (c.inf ? ", INFINI)" : ")"));
    }
}

void testInfiniteHolds()
{
    auto p = baseParams();
    p.decaySeconds = 3.0f;
    p.tone = 0.4f;
    auto rv = makeReverb (p);
    std::vector<float> inL (480), inR (480), oL (480), oR (480), wL (480), wR (480);
    Render out;
    const size_t total = (size_t) (24.0 * fs);
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> d (-0.3f, 0.3f);
    for (size_t pos = 0; pos < total; pos += 480)
    {
        const double t = (double) pos / fs;
        p.infinite = t >= 1.0; // freeze after one second of sound
        rv->setParams (p);
        for (size_t i = 0; i < 480; ++i)
        {
            // Sound keeps playing: INFINI must stop feeding the loop.
            inL[i] = t < 6.0 ? d (rng) : 0.0f;
            inR[i] = inL[i];
        }
        rv->process (inL.data(), inR.data(), oL.data(), oR.data(), wL.data(), wR.data(), 480);
        out.l.insert (out.l.end(), wL.begin(), wL.end());
        out.r.insert (out.r.end(), wR.begin(), wR.end());
    }
    const double a = db (rms (out, 2.0, 4.0)), b = db (rms (out, 20.0, 22.0));
    std::printf ("    INFINI level at 2-4 s: %.1f dB, at 20-22 s: %.1f dB\n", a, b);
    check (std::fabs (b - a) < 6.0, "INFINI holds the pad (less than 6 dB change over 18 s, decay set to 3 s)");
    check (b > -40.0, "INFINI pad is still clearly audible after 20 s");
    check (allFinite (out, 1.0f), "INFINI output stays below full scale");
}

void testInfiniteWithShimmer()
{
    auto p = baseParams();
    p.infinite = false;
    p.shimmerSemis = 12.0f;
    p.decaySeconds = 10.0f;
    auto rv = makeReverb (p);
    std::vector<float> inL (480), inR (480), oL (480), oR (480), wL (480), wR (480);
    Render out;
    for (size_t pos = 0; pos < (size_t) (40.0 * fs); pos += 480)
    {
        const double t = (double) pos / fs;
        p.infinite = t >= 1.0;
        rv->setParams (p);
        for (size_t i = 0; i < 480; ++i)
            inL[i] = inR[i] = t < 1.0 ? 0.3f * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) (pos + i) / fs) : 0.0f;
        rv->process (inL.data(), inR.data(), oL.data(), oR.data(), wL.data(), wR.data(), 480);
        out.l.insert (out.l.end(), wL.begin(), wL.end());
        out.r.insert (out.r.end(), wR.begin(), wR.end());
    }
    const double a = db (rms (out, 2, 4)), b = db (rms (out, 36, 38));
    std::printf ("    INFINI + shimmer +12: %.1f dB -> %.1f dB\n", a, b);
    check (allFinite (out, 1.0f), "INFINI + shimmer stays bounded");
    check (b > a - 15.0, "INFINI + shimmer keeps sounding");
}

void testShimmerPitch()
{
    for (float semis : { 12.0f, -12.0f, 7.0f })
    {
        auto p = baseParams();
        p.decaySeconds = 8.0f;
        p.tone = 0.8f;
        p.shimmerSemis = semis;
        auto rv = makeReverb (p);
        p.shimmerSemis = 0.0f;
        auto ref = makeReverb (p);
        const double f0 = 440.0, f1 = f0 * std::pow (2.0, semis / 12.0);
        const auto out = render (*rv, 4.0, sineBurst (f0, 1.0)), plain = render (*ref, 4.0, sineBurst (f0, 1.0));
        const double withShim = toneMagnitude (out, 1.5, 3.5, f1), without = toneMagnitude (plain, 1.5, 3.5, f1);
        std::printf ("    shimmer %+.0f: energy at %.0f Hz %+.1f dB vs no shimmer\n", semis, f1, db (withShim) - db (without));
        check (db (withShim) - db (without) > 12.0, "shimmer " + std::to_string ((int) semis) + " creates the shifted pitch");
    }
}

void testDryWet()
{
    auto p = baseParams();
    p.mix = 0.0f;
    auto rv = makeReverb (p);
    std::vector<float> in (4096), oL (4096), oR (4096);
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (auto& v : in) v = d (rng);
    rv->process (in.data(), in.data(), oL.data(), oR.data(), nullptr, nullptr, 4096);
    bool same = true;
    for (size_t i = 0; i < in.size(); ++i) same &= oL[i] == in[i] && oR[i] == in[i];
    check (same, "Dry/Wet at 0 % passes the dry signal untouched");

    p.mix = 1.0f;
    auto wet = makeReverb (p);
    const auto out = render (*wet, 0.5, sineBurst (500.0, 0.5), 512, false);
    // At 100 % wet the first samples (before the first reflections) must be silent.
    double early = 0;
    for (size_t i = 0; i < 64; ++i) early = std::max (early, (double) std::fabs (out.l[i]));
    check (early < 1e-3, "Dry/Wet at 100 % removes the dry signal");
}

void testDucking()
{
    auto p = baseParams();
    p.decaySeconds = 10.0f;
    auto noDuck = makeReverb (p);
    p.ducking = 1.0f;
    auto duck = makeReverb (p);
    // Sustained loud input from 0 to 3 s.
    const auto a = render (*noDuck, 6.0, noiseBurst (3.0, 0.5f), 512, false);
    const auto b = render (*duck, 6.0, noiseBurst (3.0, 0.5f), 512, false);
    // Compare the wet contribution while playing using the wet-only render.
    auto noDuck2 = makeReverb (baseParams());
    (void) noDuck2;
    const double playingA = rms (a, 1.5, 2.9), playingB = rms (b, 1.5, 2.9);
    const double tailA = rms (a, 4.0, 5.5), tailB = rms (b, 4.0, 5.5);
    std::printf ("    while playing: %.1f dB vs %.1f dB ducked; tail: %.1f vs %.1f dB\n", db (playingA), db (playingB), db (tailA), db (tailB));
    check (db (playingA) - db (playingB) > 1.0, "ducking lowers the mix while the input is playing");
    check (std::fabs (db (tailA) - db (tailB)) < 1.5, "ducking releases once the input stops");
}

void testBlockSizeIndependence()
{
    auto p = baseParams();
    p.shimmerSemis = 5.0f;
    auto a = makeReverb (p), b = makeReverb (p);
    const auto ra = render (*a, 1.0, noiseBurst (0.2), 1), rb = render (*b, 1.0, noiseBurst (0.2), 1024);
    double maxDiff = 0;
    for (size_t i = 0; i < ra.size(); ++i) maxDiff = std::max (maxDiff, (double) std::fabs (ra.l[i] - rb.l[i]));
    check (maxDiff < 1e-6, "output does not depend on the host block size");
}

void testStereoDecorrelation()
{
    auto rv = makeReverb (baseParams());
    const auto out = render (*rv, 3.0, noiseBurst (0.2));
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = (size_t) (0.5 * fs); i < out.size(); ++i)
    {
        lr += (double) out.l[i] * out.r[i];
        ll += (double) out.l[i] * out.l[i];
        rr += (double) out.r[i] * out.r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    std::printf ("    L/R correlation of the tail: %.2f\n", corr);
    check (std::fabs (corr) < 0.25, "wide stereo tail (decorrelated, not anti-correlated L/R)");
}

void testLevels()
{
    // Sustained noise: the wet level should stay usable whatever the decay.
    for (float decay : { 1.0f, 10.0f, 60.0f })
    {
        auto p = baseParams();
        p.decaySeconds = decay;
        auto rv = makeReverb (p);
        const auto out = render (*rv, 8.0, noiseBurst (8.0, 0.25f));
        const double rel = db (rms (out, 6.0, 8.0)) - db (0.25 / std::sqrt (3.0));
        std::printf ("    decay %2.0f s: wet level %+.1f dB relative to input\n", decay, rel);
        check (rel > -14.0 && rel < 6.0, "wet level is usable at decay " + std::to_string ((int) decay));
    }
}

void testSampleRates()
{
    for (double rate : { 44100.0, 88200.0, 192000.0 })
    {
        auto p = baseParams();
        p.shimmerSemis = 12.0f;
        auto rv = makeReverb (p, rate);
        std::vector<float> buf (512, 0.0f), wl (512), wr (512);
        bool ok = true;
        for (int b = 0; b < 400; ++b)
        {
            std::fill (buf.begin(), buf.end(), b < 20 ? 0.25f * (float) ((b * 7919) % 13) / 13.0f : 0.0f);
            std::vector<float> l = buf, r = buf;
            rv->process (l.data(), r.data(), l.data(), r.data(), wl.data(), wr.data(), 512);
            for (int i = 0; i < 512; ++i) ok &= std::isfinite (l[(size_t) i]) && std::fabs (l[(size_t) i]) < 2.0f;
        }
        check (ok, "runs cleanly at " + std::to_string ((int) rate) + " Hz");
    }
}

// --- TailRecorder ------------------------------------------------------------------------

void pushSignal (TailRecorder& rec, double seconds, float amp, bool infinite)
{
    std::vector<float> l (480, amp), r (480, -amp);
    for (size_t p = 0; p < (size_t) (seconds * fs); p += 480) rec.push (l.data(), r.data(), 480, infinite);
}

void testRecorderNormalTake()
{
    TailRecorder rec;
    rec.prepare (fs);
    pushSignal (rec, 1.0, 0.0f, false);
    pushSignal (rec, 5.0, 0.2f, false);
    pushSignal (rec, 3.0, 0.0f, false);
    const auto t = rec.renderTake();
    std::printf ("    normal take: %.2f s\n", t.seconds());
    check (std::fabs (t.seconds() - (5.0 + TailRecorder::tailPaddingSeconds)) < 0.05, "take lasts until the tail is silent");
    check (! t.truncated && t.left.front() == 0.2f && t.right.front() == -0.2f, "take starts at the sound, no fade");
    check (! rec.isTakeActive(), "take is finished after the silence");
}

void testRecorderCap60()
{
    TailRecorder rec;
    rec.prepare (fs);
    pushSignal (rec, 70.0, 0.2f, false);
    pushSignal (rec, 3.0, 0.0f, false);
    const auto t = rec.renderTake();
    std::printf ("    long take: %.2f s (truncated %d)\n", t.seconds(), (int) t.truncated);
    check (std::fabs (t.seconds() - 60.0) < 0.01, "normal take is capped at 60 s");
    check (t.truncated && std::fabs (t.left.back()) < 1e-3f, "capped take ends with a fade out");
}

void testRecorderInfinite()
{
    TailRecorder rec;
    rec.prepare (fs);
    pushSignal (rec, 45.0, 0.2f, true);
    const auto t = rec.renderTake();
    std::printf ("    INFINI take: %.2f s\n", t.seconds());
    check (std::fabs (t.seconds() - 30.0) < 0.01, "INFINI take is limited to 30 s");
    check (std::fabs (t.left.back()) < 1e-3f, "INFINI take ends with a fade out");
    const size_t mid = t.left.size() / 2;
    check (std::fabs (t.left[mid] - 0.2f) < 1e-6f, "INFINI take body is untouched");
    check (std::fabs (t.left.front()) < 0.05f, "INFINI take cut mid-sound fades in");
    check (rec.isTakeActive(), "INFINI take is still active");
}

void testRecorderNewTake()
{
    TailRecorder rec;
    rec.prepare (fs);
    pushSignal (rec, 2.0, 0.5f, false);
    pushSignal (rec, 3.0, 0.0f, false);
    pushSignal (rec, 1.0, 0.1f, false);
    pushSignal (rec, 2.0, 0.0f, false);
    const auto t = rec.renderTake();
    check (std::fabs (t.left.front() - 0.1f) < 1e-6f && t.seconds() < 1.5, "a new sound after silence starts a new take");
}

// Renders a few example files to listen to / analyse (only with --render <dir>).
void renderExamples (const std::string& dir);
} // namespace

int main (int argc, char** argv)
{
    const std::pair<const char*, void (*)()> tests[] = {
        { "Hadamard matrix", testHadamardOrthonormal },
        { "Prime delay lengths", testPrimeLengths },
        { "Decay times", testDecayTimes },
        { "Damping / tone", testTailDarkens },
        { "Low cut", testLowCut },
        { "Stability", testStability },
        { "INFINI", testInfiniteHolds },
        { "INFINI + shimmer", testInfiniteWithShimmer },
        { "Shimmer pitch", testShimmerPitch },
        { "Dry/Wet", testDryWet },
        { "Ducking", testDucking },
        { "Block size independence", testBlockSizeIndependence },
        { "Stereo", testStereoDecorrelation },
        { "Levels", testLevels },
        { "Sample rates", testSampleRates },
        { "Recorder: normal take", testRecorderNormalTake },
        { "Recorder: 60 s cap", testRecorderCap60 },
        { "Recorder: INFINI 30 s", testRecorderInfinite },
        { "Recorder: new take", testRecorderNewTake },
    };

    std::string filter;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--render" && i + 1 < argc) { renderExamples (argv[++i]); return 0; }
        filter = a;
    }

    for (const auto& [name, fn] : tests)
    {
        if (! filter.empty() && std::string (name).find (filter) == std::string::npos) continue;
        std::printf ("[%s]\n", name);
        const int before = failures;
        fn();
        std::printf ("  %s\n", failures == before ? "ok" : "FAILED");
    }
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

namespace
{
void writeWav (const std::string& path, const Render& x)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return;
    const uint32_t n = (uint32_t) x.size(), dataBytes = n * 2 * 2, rate = (uint32_t) fs;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 (rate); u32 (rate * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { x.l[i], x.r[i] })
            u16 ((uint16_t) (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f));
    std::fclose (f);
}

void renderExamples (const std::string& dir)
{
    // A short dark chord-like stab (three detuned saw-ish partials) through typical settings.
    auto chord = [] (size_t i, int ch) {
        const double t = (double) i / fs;
        if (t > 1.5) return 0.0f;
        const double env = std::min (1.0, t / 0.02) * std::exp (-t * 1.5);
        double s = 0;
        for (double f : { 65.41, 77.78, 98.0, 130.81 })
            for (int h = 1; h <= 6; ++h) s += std::sin (2 * 3.14159265 * f * h * t * (ch ? 1.002 : 1.0)) / h;
        return (float) (0.08 * env * s);
    };
    struct Ex { const char* name; Params p; double seconds; };
    Params atrium = baseParams(); atrium.size = 0.85f; atrium.decaySeconds = 25.0f; atrium.tone = 0.25f;
    Params shimmerUp = atrium; shimmerUp.shimmerSemis = 12.0f;
    Params shimmerDown = atrium; shimmerDown.shimmerSemis = -12.0f;
    Params smallBright = baseParams(); smallBright.size = 0.2f; smallBright.decaySeconds = 3.0f; smallBright.tone = 0.9f;
    const Ex examples[] = { { "atrium_dark_25s", atrium, 30.0 }, { "shimmer_up", shimmerUp, 20.0 },
                            { "shimmer_down", shimmerDown, 20.0 }, { "small_bright", smallBright, 6.0 } };
    for (const auto& e : examples)
    {
        auto rv = makeReverb (e.p);
        const auto out = render (*rv, e.seconds, chord);
        writeWav (dir + "/" + e.name + ".wav", out);
        std::printf ("%s: T60 %.1f s, bright early %.3f late %.4f, rms 2-4 s %.1f dB, 15-17 s %.1f dB\n", e.name,
                     estimateT60 (out), brightness (out, 1.5, 3), brightness (out, 10, 14), db (rms (out, 2, 4)),
                     db (rms (out, 15, 17)));
    }
}
} // namespace
