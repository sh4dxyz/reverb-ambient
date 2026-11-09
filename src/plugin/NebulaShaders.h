#pragma once

// GLSL sources for the nebula / black hole background. Written in GLSL 1.20 style and
// translated to 1.50 at runtime by juce::OpenGLHelpers when a 3.2 core context is used.
// (Avoid the words "varying", "texture2D" and "gl_FragColor" outside of their intended
// use: the translator does a plain text replace.)

namespace NebulaShaders
{
inline constexpr const char* vertex = R"GLSL(
attribute vec2 position;
varying vec2 vUv;
void main()
{
    vUv = position * 0.5 + 0.5;
    gl_Position = vec4 (position, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* fragment = R"GLSL(
varying vec2 vUv;

uniform vec2  uRes;
uniform float uTime;
uniform float uLevel;    // overall wet level 0..1
uniform float uLow;      // low band energy 0..1
uniform float uHigh;     // high band energy 0..1
uniform float uInput;    // dry input level 0..1
uniform float uSize;     // Taille 0..1
uniform float uDecay;    // Duree 0..1
uniform float uTone;     // Sombre/Clair 0..1
uniform float uShimmer;  // -1..1
uniform float uDuck;     // Ducking 0..1
uniform float uInf;      // INFINI morph 0..1
// Phases integrated on the CPU (speed x dt summed every frame), so changing a speed only
// changes the speed: multiplying the absolute time by a changing factor made the image
// jump and spin wildly when INFINI or Duree moved.
uniform float uFlow;     // gas drift
uniform float uSwirl;    // black hole rotation
uniform vec2  uMouse;    // pixels
uniform vec3  uRipple0;  // x, y (pixels), age (s)
uniform vec3  uRipple1;
uniform vec3  uRipple2;
uniform vec3  uRipple3;

float hash (vec2 p)
{
    p = fract (p * vec2 (123.34, 456.21));
    p += dot (p, p + 45.32);
    return fract (p.x * p.y);
}

float noise (vec2 p)
{
    vec2 i = floor (p);
    vec2 f = fract (p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix (mix (hash (i), hash (i + vec2 (1.0, 0.0)), u.x),
                mix (hash (i + vec2 (0.0, 1.0)), hash (i + vec2 (1.0, 1.0)), u.x), u.y);
}

float fbm (vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    mat2 m = mat2 (1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 5; ++i)
    {
        v += a * noise (p);
        p = m * p;
        a *= 0.5;
    }
    return v;
}

mat2 rot (float a)
{
    float c = cos (a);
    float s = sin (a);
    return mat2 (c, -s, s, c);
}

vec2 rippleWarp (vec2 p, vec3 rp, inout float glow)
{
    if (rp.z < 0.0 || rp.z > 3.0)
        return vec2 (0.0);
    vec2 c = (rp.xy - 0.5 * uRes) / uRes.y;
    vec2 d = p - c;
    float r = length (d);
    float front = rp.z * 0.55;
    float k = (r - front) * 38.0; // pow() is undefined for negative bases in GLSL
    float w = exp (-k * k) * (1.0 - rp.z / 3.0);
    glow += w;
    return d / max (r, 0.0001) * w * 0.05;
}

vec3 stars (vec2 q, float density, float twinkleBoost)
{
    vec3 acc = vec3 (0.0);
    for (int layer = 0; layer < 2; ++layer)
    {
        float fl = float (layer);
        vec2 g = q * (55.0 + fl * 70.0) + fl * 17.3;
        vec2 cell = floor (g);
        float h = hash (cell);
        if (h > density)
        {
            vec2 pos = vec2 (hash (cell + 3.1), hash (cell + 7.7));
            float d = length (fract (g) - pos);
            float tw = 0.6 + 0.4 * sin (uTime * (1.0 + h * 5.0) + h * 40.0);
            float b = smoothstep (0.09, 0.0, d) * tw * (0.5 + twinkleBoost);
            acc += mix (vec3 (0.75, 0.8, 1.0), vec3 (1.0, 0.8, 0.65), hash (cell + 1.9)) * b;
        }
    }
    return acc;
}

void main()
{
    vec2 frag = vUv * uRes;
    vec2 p = (frag - 0.5 * uRes) / uRes.y;
    float t = uTime;
    float inf = clamp (uInf, 0.0, 1.0);

    // Click ripples (visual only).
    float rippleGlow = 0.0;
    vec2 warp = rippleWarp (p, uRipple0, rippleGlow) + rippleWarp (p, uRipple1, rippleGlow)
              + rippleWarp (p, uRipple2, rippleGlow) + rippleWarp (p, uRipple3, rippleGlow);
    p += warp;

    // The cursor gently pulls the gas.
    vec2 m = (uMouse - 0.5 * uRes) / uRes.y;
    vec2 toM = m - p;
    p += toM * 0.04 * exp (-dot (toM, toM) * 6.0);

    float r = length (p);

    // INFINI: the nebula collapses into a black hole (lensing + swirl).
    float rs = inf * (0.085 + 0.05 * uSize);
    vec2 q = p;
    q *= 1.0 + inf * rs * rs * 2.2 / max (r * r, 0.0004);
    q = rot (inf * (1.6 / (r * 5.0 + 0.35)) + uSwirl) * q;

    // Ducking breathes the cloud in when the dry signal plays.
    q *= 1.0 + uDuck * uInput * 0.25;

    float scale = mix (2.8, 1.15, uSize);
    vec2 np = q * scale + vec2 (0.0, 0.2);

    vec2 w1 = vec2 (fbm (np + vec2 (0.0, uFlow)), fbm (np + vec2 (5.2, 1.3) - uFlow * 0.7));
    vec2 w2 = vec2 (fbm (np + 3.0 * w1 + vec2 (1.7, 9.2) + uFlow * 0.4),
                    fbm (np + 3.0 * w1 + vec2 (8.3, 2.8) - uFlow * 0.3));
    float n = fbm (np + 2.6 * w2);

    // Gas density with dark dust lanes cutting through it.
    float dens = smoothstep (0.2, 0.82, n);
    float dust = smoothstep (0.45, 0.75, fbm (np * 2.3 + w1 * 1.5 + vec2 (3.1, 7.4)));
    dens *= 1.0 - 0.6 * dust;
    dens *= 0.7 + 0.8 * exp (-dot (q, q) * 0.9);

    // Three-tone palettes. Sombre: oxblood, magenta, ember. Clair: deep teal, cyan, gold.
    vec3 c1 = mix (vec3 (0.16, 0.01, 0.05), vec3 (0.01, 0.10, 0.16), uTone);
    vec3 c2 = mix (vec3 (0.62, 0.05, 0.28), vec3 (0.08, 0.55, 0.78), uTone);
    vec3 c3 = mix (vec3 (1.00, 0.42, 0.18), vec3 (1.00, 0.84, 0.45), uTone);
    float hue = clamp (w2.y * 1.6 - 0.3, 0.0, 1.0);
    vec3 gas = mix (c1, c2, smoothstep (0.0, 0.6, dens));
    gas = mix (gas, c3, smoothstep (0.55, 1.0, dens) * hue);

    float energy = 0.6 + 2.2 * uLevel;
    vec3 col = gas * dens * energy;
    // Bright filaments along the warped ridges react to the highs.
    float ridge = 1.0 - abs (n * 2.0 - 1.1);
    col += c3 * pow (clamp (ridge, 0.0, 1.0), 8.0) * dens * (0.25 + 2.5 * uHigh);
    // Faint violet background haze so the dark areas are never flat black.
    col += vec3 (0.035, 0.015, 0.06) * (0.4 + fbm (p * 1.3 + 9.0));

    // Small pulsing core (follows the low end).
    float core = exp (-r * r * 70.0) * (0.05 + 0.7 * uLow) + exp (-r * r * 900.0) * 0.6 * uLow;
    col += mix (vec3 (1.0, 0.5, 0.6), vec3 (0.75, 0.95, 1.0), uTone) * core * (1.0 - inf);

    // Lensed star field.
    col += stars (q * 0.8, 0.968, uHigh * 2.0) * (1.0 - 0.7 * dens);

    // Shimmer sparks: rising for +, falling for -.
    float sh = abs (uShimmer);
    if (sh > 0.01)
    {
        float dir = uShimmer > 0.0 ? 1.0 : -1.0;
        vec2 g = (q + vec2 (0.0, -dir * t * 0.09)) * 26.0;
        vec2 cell = floor (g);
        float h = hash (cell + 11.0);
        if (h > 0.94)
        {
            vec2 pos = vec2 (hash (cell + 2.0), hash (cell + 5.0));
            float d = length (fract (g) - pos);
            float flick = 0.5 + 0.5 * sin (t * 9.0 + h * 60.0);
            col += mix (vec3 (0.7, 0.85, 1.0), vec3 (1.0, 0.85, 0.5), uTone) * smoothstep (0.12, 0.0, d) * flick * sh * (0.6 + 2.0 * uLevel);
        }
    }

    if (inf > 0.001)
    {
        // Horizon swallows everything behind it.
        float horizon = smoothstep (rs * 0.97, rs * 1.03, r);
        col *= mix (1.0, horizon, inf);

        // Thin tilted accretion disk. The texture is sampled in a rotating cartesian frame
        // (no angular seam) and the approaching side is brighter (Doppler beaming).
        vec2 dp = vec2 (p.x, p.y * 5.0);
        float dr = length (dp);
        vec2 sw = rot (t * 0.5 + 2.5 / (dr / max (rs, 0.001) + 0.5)) * dp;
        float gasDisk = fbm (sw * 9.0 / max (rs * 4.0, 0.05) + vec2 (t * 0.2, 0.0));
        float radial = smoothstep (rs * 1.25, rs * 1.6, dr) * exp (-(dr - rs * 1.4) / (rs * 1.4));
        float beaming = 1.0 + 0.7 * (-p.x / max (dr, 0.001));
        float diskI = radial * (0.35 + 1.3 * gasDisk) * beaming;
        vec3 diskCol = mix (vec3 (1.0, 0.30, 0.06), vec3 (1.0, 0.93, 0.80), clamp (radial * 1.3, 0.0, 1.0));
        float front = p.y < 0.0 ? 1.0 : horizon;
        col += diskCol * diskI * front * inf * (0.9 + 1.8 * uLevel);

        // Far side of the disk, lensed into an arc over the top of the horizon.
        float ak = (r - rs * 1.45) / (rs * 0.28);
        float arc = exp (-ak * ak) * smoothstep (-0.2 * rs, 0.6 * rs, p.y) * (0.5 + gasDisk);
        col += vec3 (1.0, 0.62, 0.30) * arc * 0.9 * inf * horizon;

        // Photon ring.
        float rk = (r - rs * 1.05) / (rs * 0.03 + 0.001);
        col += vec3 (1.0, 0.88, 0.70) * exp (-rk * rk) * 1.4 * inf;
    }

    col += mix (vec3 (1.0, 0.35, 0.75), vec3 (0.35, 0.9, 1.0), uTone) * rippleGlow * 0.22;

    // Tone map, vignette, dither.
    col = vec3 (1.0) - exp (-col * 1.25);
    float vig = smoothstep (1.25, 0.25, length (p * vec2 (0.85, 1.05)));
    col *= 0.25 + 0.75 * vig;
    col = pow (col, vec3 (0.92));
    col += (hash (frag + fract (t) * 100.0) - 0.5) / 255.0;

    gl_FragColor = vec4 (col, 1.0);
}
)GLSL";
} // namespace NebulaShaders
