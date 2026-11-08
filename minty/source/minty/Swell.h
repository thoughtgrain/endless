#pragma once

// Dynamic swell, the default AUX behaviour on Clean: when playing crosses the
// SENSITIVITY threshold the volume fades in, when it drops away the volume
// fades out. A fresh pick attack over a ringing note re-triggers the swell
// (fast duck, then fade in again) so every note gets the violin treatment,
// not just the first one after silence. The engine's 1.5 ms lookahead means
// the duck finishes before the pick actually reaches the output.
// Re-triggering is a per-profile switch (see InputProfile.h): on for guitar,
// off for synth, where it would duck every note of a fast sequence.

#include "DspMath.h"
#include "InputProfile.h"

namespace minty
{

class Swell
{
  public:
    void reset()
    {
        level = enabled ? 0.0f : 1.0f;
        meanAbs = 0.0f;
        playing = false;
        ducking = false;
        quietSamples = 0;
        samplesSinceTrigger = kRetriggerHoldoff;
    }

    void setEnabled(bool on) { enabled = on; }
    bool isEnabled() const { return enabled; }

    // peakLin: stereo-linked peak detector from the compressor (linear)
    // levelDb / thresholdDb: same detector in dB and the SENSITIVITY threshold
    float process(float peakLin, float levelDb, float thresholdDb)
    {
        meanAbs += kMeanCoef * (peakLin - meanAbs);
        if (samplesSinceTrigger < kRetriggerHoldoff)
        {
            ++samplesSinceTrigger;
        }

        if (!enabled)
        {
            level += kBypassRate * (1.0f - level);
            playing = false;
            ducking = false;
            return level;
        }

        // Gate-style hysteresis so a decaying note doesn't chatter.
        if (levelDb > thresholdDb - kOpenBelowDb)
        {
            if (!playing)
            {
                playing = true;
                samplesSinceTrigger = 0;
            }
            quietSamples = 0;
        }
        else if (levelDb < thresholdDb - kCloseBelowDb)
        {
            if (++quietSamples > kQuietHold)
            {
                playing = false;
            }
        }

        // Re-trigger on a new pick attack: the instant peak jumps well above
        // the recent average while we are already open.
        if (kProfile.swellRetrigger && playing && !ducking && samplesSinceTrigger >= kRetriggerHoldoff &&
            level > 0.35f && peakLin > meanAbs * kTransientRatio)
        {
            ducking = true;
            samplesSinceTrigger = 0;
        }

        if (ducking)
        {
            level -= kDuckRate;
            if (level <= 0.0f)
            {
                level = 0.0f;
                ducking = false;
            }
        }
        else if (playing)
        {
            level = level + kInRate < 1.0f ? level + kInRate : 1.0f;
        }
        else
        {
            level = level - kOutRate > 0.0f ? level - kOutRate : 0.0f;
        }

        // Squared ramp feels like a volume pedal rather than a linear fader.
        return level * level;
    }

  private:
    static constexpr float kSwellInSec = 0.45f;
    static constexpr float kSwellOutSec = 0.6f;
    static constexpr float kInRate = 1.0f / (kSwellInSec * kSampleRate);
    static constexpr float kOutRate = 1.0f / (kSwellOutSec * kSampleRate);
    static constexpr float kDuckRate = 1.0f / (0.0012f * kSampleRate); // 1.2 ms duck, inside the lookahead
    static constexpr float kBypassRate = 1.0f / (0.02f * kSampleRate);
    static constexpr float kMeanCoef = 1.0f / (0.06f * kSampleRate);  // ~60 ms average
    static constexpr float kTransientRatio = 3.2f;                    // ~10 dB jump
    static constexpr float kOpenBelowDb = 4.0f;
    static constexpr float kCloseBelowDb = 14.0f;
    static constexpr int kQuietHold = static_cast<int>(0.03f * kSampleRate);
    static constexpr int kRetriggerHoldoff = static_cast<int>(0.15f * kSampleRate);

    bool enabled = false;
    bool playing = false;
    bool ducking = false;
    int quietSamples = 0;
    int samplesSinceTrigger = kRetriggerHoldoff;
    float level = 1.0f;
    float meanAbs = 0.0f;
};

} // namespace minty
