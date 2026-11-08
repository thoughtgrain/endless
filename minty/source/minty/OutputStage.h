#pragma once

// Stage 2: the final limiter, or (in Dusty) a soft, crumbly clipper.
// Either way the last thing before the host is a hard safety ceiling, because
// the SDK's own advice is that anything outside (-1, 1) will glitch.

#include "DspMath.h"
#include "Filters.h"
#include "InputProfile.h"

namespace minty
{

class OutputStage
{
  public:
    void reset()
    {
        limGain = 1.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            dcBlock[ch].setCutoff(12.0f);
            dcBlock[ch].reset();
            fizz[ch].setCutoff(6500.0f);
            fizz[ch].reset();
        }
    }

    void setDusty(bool on) { dusty = on; }

    // drive01: SENSITIVITY knob, doubles as the Dusty drive amount.
    void setDrive(float drive01)
    {
        driveGain = dbToLin(lerp(kProfile.dustyDriveMinDb, kProfile.dustyDriveMaxDb, clampf(drive01, 0.0f, 1.0f)));
        // Keep perceived level roughly constant as drive goes up.
        dustyTrim = 0.7f / (0.55f + 0.45f * std::sqrt(driveGain));
    }

    void process(float& l, float& r)
    {
        if (dusty)
        {
            l = fizz[0].process(dcBlock[0].process(crumble(l * driveGain))) * dustyTrim * kProfile.dustyLevel;
            r = fizz[1].process(dcBlock[1].process(crumble(r * driveGain))) * dustyTrim * kProfile.dustyLevel;
        }

        // Peak limiter: instant attack, ~60 ms recovery, stereo linked.
        const float al = std::fabs(l), ar = std::fabs(r);
        const float peak = al > ar ? al : ar;
        const float target = peak > kCeiling ? kCeiling / peak : 1.0f;
        limGain = target < limGain ? target : limGain + kLimRelease * (target - limGain);
        l = safety(l * limGain);
        r = safety(r * limGain);
    }

    float limiterReductionDb() const { return -linToDb(limGain); }

  private:
    // Asymmetric soft clip: the negative half folds in a bit harder, which
    // adds even harmonics, the "crumbly" part.
    static float crumble(float x)
    {
        if (x >= 0.0f)
        {
            return x / (1.0f + x);
        }
        const float y = 1.35f * x;
        return y / (1.0f - y) * 0.82f;
    }

    // Cubic soft saturation above 0.9, hard ceiling at 0.98. Belt and braces.
    static float safety(float x)
    {
        if (!isFiniteF(x))
        {
            return 0.0f;
        }
        const float a = std::fabs(x);
        if (a <= kKnee)
        {
            return x;
        }
        const float t = clampf((a - kKnee) / (1.0f - kKnee), 0.0f, 1.0f);
        const float shaped = kKnee + (kHard - kKnee) * (t * (1.5f - 0.5f * t * t));
        return x > 0.0f ? shaped : -shaped;
    }

    static constexpr float kCeiling = 0.89f; // ~ -1 dBFS
    static constexpr float kKnee = 0.9f;
    static constexpr float kHard = 0.98f;
    static constexpr float kLimRelease = 1.0f / (0.06f * kSampleRate);

    bool dusty = false;
    float driveGain = 2.0f;
    float dustyTrim = 1.0f;
    float limGain = 1.0f;
    OnePoleHighpass dcBlock[2];
    OnePoleLowpass fizz[2];
};

} // namespace minty
