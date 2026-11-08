#pragma once

// Continuously records the wet output into a ring buffer and tracks "takes":
// a take starts when the reverb output rises above the silence threshold and ends when
// it has been silent for a while. The message thread can then render the latest take
// into a buffer for WAV export:
//   - normal mode : from the take start until the tail is silent, at most 60 s
//                   (with a fade out if the cap is reached);
//   - INFINI mode : the most recent 30 s at most, always with a fade out.
//
// Single writer (audio thread) / single reader (message thread). Positions are absolute
// 64-bit sample counters published through atomics; the ring holds more than the longest
// export so the reader never sees samples being overwritten. prepare() and renderTake()
// share a mutex (never taken by the audio thread) so the buffers cannot be reallocated
// while a take is being copied.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rvb
{
class TailRecorder
{
public:
    static constexpr double maxTakeSeconds = 60.0;
    static constexpr double infiniteTakeSeconds = 30.0;
    static constexpr double ringSeconds = 66.0;
    static constexpr double silenceHoldSeconds = 1.5;
    static constexpr double tailPaddingSeconds = 0.25;
    static constexpr double normalFadeSeconds = 2.0;
    static constexpr double infiniteFadeSeconds = 4.0;
    static constexpr double fadeInSeconds = 0.02;
    static constexpr float silenceThreshold = 0.000316f; // -70 dBFS

    void prepare (double newSampleRate)
    {
        const std::lock_guard<std::mutex> lock (bufferMutex);
        sampleRate = newSampleRate;
        ringSize = (int64_t) std::ceil (ringSeconds * sampleRate);
        left.assign ((size_t) ringSize, 0.0f);
        right.assign ((size_t) ringSize, 0.0f);
        silenceHold = (int64_t) (silenceHoldSeconds * sampleRate);
        reset();
    }

    void reset()
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        written.store (0);
        takeStart.store (-1);
        lastLoud.store (-1);
        infiniteActive.store (false);
    }

    // Audio thread.
    void push (const float* l, const float* r, int n, bool infinite)
    {
        if (ringSize == 0) return;
        int64_t pos = written.load (std::memory_order_relaxed);
        int64_t start = takeStart.load (std::memory_order_relaxed);
        int64_t loud = lastLoud.load (std::memory_order_relaxed);

        for (int i = 0; i < n; ++i, ++pos)
        {
            const float a = l[i], b = r != nullptr ? r[i] : a;
            const size_t idx = (size_t) (pos % ringSize);
            left[idx] = a;
            right[idx] = b;

            if (std::max (std::fabs (a), std::fabs (b)) > silenceThreshold)
            {
                // A new take begins after a silence (or at the very first sound).
                if (start < 0 || loud < 0 || pos - loud > silenceHold)
                    start = pos;
                loud = pos;
            }
        }

        takeStart.store (start, std::memory_order_relaxed);
        lastLoud.store (loud, std::memory_order_relaxed);
        infiniteActive.store (infinite, std::memory_order_relaxed);
        written.store (pos, std::memory_order_release);
    }

    struct Take
    {
        std::vector<float> left, right;
        double sampleRate = 0.0;
        bool infinite = false;
        bool truncated = false;
        double seconds() const { return sampleRate > 0 ? (double) left.size() / sampleRate : 0.0; }
    };

    bool hasTake() const { return takeStart.load() >= 0; }

    // True while the current take is still sounding.
    bool isTakeActive() const
    {
        const int64_t loud = lastLoud.load();
        return loud >= 0 && written.load() - loud <= silenceHold;
    }

    // Length (s) the export would have right now.
    double currentTakeSeconds() const
    {
        int64_t s, e; bool trunc;
        if (! computeRange (s, e, trunc)) return 0.0;
        return (double) (e - s) / sampleRate;
    }

    // Message thread: copy the latest take, with fades applied.
    Take renderTake() const
    {
        const std::lock_guard<std::mutex> lock (bufferMutex);
        Take t;
        t.sampleRate = sampleRate;
        t.infinite = infiniteActive.load();
        int64_t s, e;
        if (! computeRange (s, e, t.truncated)) return t;

        const size_t n = (size_t) (e - s);
        t.left.resize (n);
        t.right.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) ((s + (int64_t) i) % ringSize);
            t.left[i] = left[idx];
            t.right[i] = right[idx];
        }

        // Fade in only when we cut into a sound that had already started.
        if (s > takeStart.load())
            applyFade (t, 0, std::min (n, (size_t) (fadeInSeconds * sampleRate)), true);

        if (t.infinite || t.truncated)
        {
            const double fadeSec = t.infinite ? infiniteFadeSeconds : normalFadeSeconds;
            const size_t fadeLen = std::min (n, (size_t) (fadeSec * sampleRate));
            applyFade (t, n - fadeLen, n, false);
        }
        return t;
    }

private:
    bool computeRange (int64_t& s, int64_t& e, bool& truncated) const
    {
        truncated = false;
        const int64_t pos = written.load (std::memory_order_acquire);
        const int64_t start = takeStart.load();
        const int64_t loud = lastLoud.load();
        if (start < 0 || loud < 0) return false;

        const bool infinite = infiniteActive.load();
        const bool ended = pos - loud > silenceHold;

        s = std::max (start, pos - ringSize + 1);
        e = ended ? std::min (pos, loud + 1 + (int64_t) (tailPaddingSeconds * sampleRate)) : pos;

        const int64_t cap = (int64_t) ((infinite ? infiniteTakeSeconds : maxTakeSeconds) * sampleRate);
        if (e - s > cap)
        {
            truncated = true;
            // INFINI or a take still sounding: keep the most recent part. Otherwise keep the
            // first 60 s of the finished take.
            if (infinite || ! ended) s = e - cap; else e = s + cap;
        }
        return e > s;
    }

    static void applyFade (Take& t, size_t from, size_t to, bool fadeIn)
    {
        const size_t len = to - from;
        if (len == 0) return;
        for (size_t i = 0; i < len; ++i)
        {
            const float x = (float) (i + 1) / (float) len;
            // Equal-power cosine curve.
            const float g = fadeIn ? std::sin (x * 1.5707963f) : std::cos (x * 1.5707963f);
            t.left[from + i] *= g;
            t.right[from + i] *= g;
        }
    }

    double sampleRate = 48000.0;
    int64_t ringSize = 0;
    int64_t silenceHold = 0;
    std::vector<float> left, right;
    std::atomic<int64_t> written { 0 }, takeStart { -1 }, lastLoud { -1 };
    std::atomic<bool> infiniteActive { false };
    mutable std::mutex bufferMutex;
};
} // namespace rvb
