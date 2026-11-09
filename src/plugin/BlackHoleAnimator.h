#pragma once

#include <algorithm>
#include <cmath>

// Time line of the black hole drawn when FREEZE is engaged, advanced once per UI frame.
//
//   on : infall (gas dragged in, core brightens) -> collapse flash -> horizon snaps open
//        with a small elastic overshoot -> accretion disk lights up from the inside out
//   off: horizon shrinks, accelerating -> evaporation flash -> shock wave pushes the gas
//        out while the disk is flung away and fades
struct BlackHoleAnimator
{
    // Outputs (uniforms).
    float hole = 0.0f, disk = 0.0f, spread = 0.0f, pull = 0.0f, flash = 0.0f, burst = -1.0f;
    float freezeAmount = 0.0f; // slows the gas drift

    void advance (bool frozen, float dt)
    {
        if (frozen != on)
        {
            on = frozen;
            t = 0.0f;
            holeAtToggle = hole;
            diskAtToggle = disk;
            freezeAtToggle = freezeAmount;
        }
        t += dt;

        if (on)
        {
            pull = smooth (0.0f, 0.22f, t) * (1.0f - smooth (0.28f, 0.7f, t));
            // If it was still evaporating, reopen from there rather than from nothing.
            const float open = t < collapseAt ? 0.0f : easeOutBack (std::min (1.0f, (t - collapseAt) / 0.55f));
            hole = std::max (holeAtToggle * (1.0f - smooth (0.0f, collapseAt, t)), open);
            flash = holeAtToggle < 0.5f ? gauss (t, collapseAt + 0.02f, 0.07f) : 0.0f;
            disk = std::max (diskAtToggle, smooth (collapseAt, 1.8f, t));
            spread = 0.0f;
            burst = -1.0f;
            freezeAmount = std::max (freezeAtToggle, smooth (0.0f, 1.0f, t));
        }
        else
        {
            const float shrink = std::min (1.0f, t / 0.4f);
            hole = holeAtToggle * (1.0f - shrink * shrink * shrink);
            flash = 0.8f * holeAtToggle * gauss (t, 0.4f, 0.06f);
            burst = holeAtToggle > 0.2f ? t - 0.4f : -1.0f;
            disk = diskAtToggle * (1.0f - smooth (0.2f, 1.4f, t));
            spread = diskAtToggle * smooth (0.1f, 1.4f, t);
            pull = 0.0f;
            freezeAmount = freezeAtToggle * (1.0f - smooth (0.0f, 1.0f, t));
        }
    }

    // Jump straight to the resting state (editor opened while frozen).
    void settle (bool frozen)
    {
        on = frozen;
        t = 100.0f;
        holeAtToggle = diskAtToggle = freezeAtToggle = frozen ? 1.0f : 0.0f;
        advance (frozen, 0.0f);
    }

private:
    static constexpr float collapseAt = 0.3f;

    static float smooth (float a, float b, float x)
    {
        const float u = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return u * u * (3.0f - 2.0f * u);
    }
    static float gauss (float x, float centre, float width)
    {
        const float k = (x - centre) / width;
        return std::exp (-k * k);
    }
    static float easeOutBack (float x)
    {
        const float c = 1.9f, y = x - 1.0f;
        return 1.0f + (c + 1.0f) * y * y * y + c * y * y;
    }

    bool on = false;
    float t = 100.0f, holeAtToggle = 0.0f, diskAtToggle = 0.0f, freezeAtToggle = 0.0f;
};
