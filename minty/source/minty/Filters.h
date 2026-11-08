#pragma once

#include "DspMath.h"

namespace minty
{

// First-order highpass (one-pole lowpass subtracted from the input).
// Used to keep bass out of the compressor's control signal.
struct OnePoleHighpass
{
    float coef = 0.0f;
    float z = 0.0f;

    void setCutoff(float hz) { coef = 1.0f - std::exp(-2.0f * kPi * hz / kSampleRate); }
    void reset() { z = 0.0f; }

    float process(float x)
    {
        z += coef * (x - z);
        return x - z;
    }
};

struct OnePoleLowpass
{
    float coef = 1.0f;
    float z = 0.0f;

    void setCutoff(float hz) { coef = 1.0f - std::exp(-2.0f * kPi * hz / kSampleRate); }
    void reset() { z = 0.0f; }

    float process(float x)
    {
        z += coef * (x - z);
        return z;
    }
};

// Topology-preserving-transform state variable filter (Zavalishin).
// Stays stable under fast cutoff modulation, which is exactly what the
// dynamic EQ does to it.
class Svf
{
  public:
    enum class Mode
    {
        kLowpass,
        kHighpass
    };

    void setMode(Mode m) { mode = m; }

    void set(float cutoffHz, float q)
    {
        const float fc = clampf(cutoffHz, 10.0f, 0.45f * kSampleRate);
        g = std::tan(kPi * fc / kSampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset()
    {
        ic1 = 0.0f;
        ic2 = 0.0f;
    }

    float process(float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return mode == Mode::kLowpass ? v2 : v0 - k * v1 - v2;
    }

  private:
    Mode mode = Mode::kLowpass;
    float g = 0.0f, k = 1.414f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

} // namespace minty
