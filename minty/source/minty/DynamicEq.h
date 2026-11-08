#pragma once

// One-knob dynamic EQ in Clean's "Shifty" style.
//   knob < noon : lowpass closes down (darker)
//   noon        : flat
//   knob > noon : highpass opens up (thinner)
// When you play above the SENSITIVITY threshold the filter slides back toward
// full range, and drifts back to the knob setting as you ease off.

#include "DspMath.h"
#include "Filters.h"

namespace minty
{

class DynamicEq
{
  public:
    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            lp[ch].setMode(Svf::Mode::kLowpass);
            hp[ch].setMode(Svf::Mode::kHighpass);
            lp[ch].reset();
            hp[ch].reset();
        }
        shift = 0.0f;
        updateCoefficients();
    }

    void setKnob(float v) { knob = clampf(v, 0.0f, 1.0f); }

    // Per sample: follow whether we're above threshold.
    void trackDynamics(float levelDb, float thresholdDb)
    {
        const float target = smoothstep(thresholdDb - 6.0f, thresholdDb + 6.0f, levelDb);
        shift += (target > shift ? kShiftAttack : kShiftRelease) * (target - shift);
    }

    // Control rate: recompute filter cutoffs from knob + current shift.
    void updateCoefficients()
    {
        float tilt = (knob - 0.5f) * 2.0f; // -1 .. 1
        // Small dead zone so "noon" is reliably flat on a real pot.
        if (tilt > -kDeadZone && tilt < kDeadZone)
        {
            tilt = 0.0f;
        }
        else
        {
            tilt = tilt > 0.0f ? (tilt - kDeadZone) / (1.0f - kDeadZone)
                               : (tilt + kDeadZone) / (1.0f - kDeadZone);
        }

        const float open = 1.0f - kShiftDepth * shift;
        const float dark = (tilt < 0.0f ? -tilt : 0.0f) * open;
        const float thin = (tilt > 0.0f ? tilt : 0.0f) * open;

        // Exponential sweeps so the knob feels even across its travel.
        const float lpHz = kLpOpenHz * std::exp(dark * std::log(kLpClosedHz / kLpOpenHz));
        const float hpHz = kHpOpenHz * std::exp(thin * std::log(kHpClosedHz / kHpOpenHz));

        for (int ch = 0; ch < 2; ++ch)
        {
            lp[ch].set(lpHz, kQ);
            hp[ch].set(hpHz, kQ);
        }
    }

    void process(float& l, float& r)
    {
        l = hp[0].process(lp[0].process(l));
        r = hp[1].process(lp[1].process(r));
    }

  private:
    static constexpr float kDeadZone = 0.05f;
    static constexpr float kShiftDepth = 0.85f;
    static constexpr float kLpOpenHz = 20000.0f;
    static constexpr float kLpClosedHz = 550.0f;
    static constexpr float kHpOpenHz = 12.0f;
    static constexpr float kHpClosedHz = 1400.0f;
    static constexpr float kQ = 0.8f;
    static constexpr float kShiftAttack = 1.0f / (0.03f * kSampleRate);
    static constexpr float kShiftRelease = 1.0f / (0.35f * kSampleRate);

    Svf lp[2], hp[2];
    float knob = 0.5f;
    float shift = 0.0f;
};

} // namespace minty
