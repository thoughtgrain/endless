#pragma once

// Small, allocation-free math helpers shared by every Minty block.
// Everything here is float-only on purpose: the Cortex-M7 FPU (fpv5-sp-d16)
// is single precision, and the SDK builds with -Wdouble-promotion -Werror.

#include <cmath>
#include <cstdint>

namespace minty
{

constexpr float kSampleRate = 48000.0f;
constexpr float kPi = 3.14159265358979f;

inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float smoothstep(float edge0, float edge1, float x)
{
    const float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float dbToLin(float db)
{
    return std::exp(db * 0.115129254649702f); // ln(10) / 20
}

inline float linToDb(float lin)
{
    return 8.68588963806504f * std::log(lin > 1.0e-9f ? lin : 1.0e-9f); // 20 / ln(10)
}

// One-pole smoothing coefficient for y += c * (x - y), time constant in seconds.
inline float timeToCoef(float seconds)
{
    return 1.0f - std::exp(-1.0f / (seconds * kSampleRate));
}

// Same as timeToCoef but cheap enough to call every sample. For the time
// constants used here (>= 0.5 ms) x <= 0.042, and the 2nd-order Taylor
// expansion of 1 - e^-x is accurate to better than 0.03 %.
inline float timeToCoefFast(float seconds)
{
    const float x = 1.0f / (seconds * kSampleRate);
    return x * (1.0f - 0.5f * x);
}

// Soft-knee "amount over threshold" in dB. Returns 0 below the knee,
// (x - threshold) above it, and a quadratic blend inside the knee.
inline float softOver(float xDb, float thresholdDb, float kneeDb)
{
    const float d = xDb - thresholdDb;
    const float halfKnee = 0.5f * kneeDb;
    if (d <= -halfKnee)
    {
        return 0.0f;
    }
    if (d >= halfKnee)
    {
        return d;
    }
    const float t = d + halfKnee;
    return t * t / (2.0f * kneeDb);
}

inline bool isFiniteF(float x)
{
    return std::isfinite(x);
}

// Linear congruential noise in [-1, 1). Deterministic, no state beyond 32 bits.
struct Noise
{
    uint32_t state = 0x9E3779B9u;

    float next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(state)) * (1.0f / 2147483648.0f);
    }
};

} // namespace minty
