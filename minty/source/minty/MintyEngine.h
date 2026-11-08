#pragma once

// Minty: the whole signal chain, independent of the Endless host API so it
// can be unit-tested and rendered on a desktop machine.
//
//   in ─┬─► 1.5 ms delay ──► × gain × swell ──► Dynamic EQ ──► Output stage ──► out
//       │                       ▲        ▲          ▲            (limiter / Dusty)
//       └─► detector ──► Compressor ──► Swell ──────┘
//           (HPF 90 Hz)   level + threshold also drive the EQ shift
//
// The detector sees the input 1.5 ms before the audio path does. That small
// lookahead lets the compressor and the swell duck land *before* a pick
// attack instead of just after it.
//
// Knobs are smoothed per sample; everything derived from them is recomputed
// every kControlInterval samples so the expensive bits (tan, exp) stay off
// the per-sample path.

#include "Compressor.h"
#include "DspMath.h"
#include "DynamicEq.h"
#include "InputProfile.h"
#include "OutputStage.h"
#include "Swell.h"

namespace minty
{

class MintyEngine
{
  public:
    enum Knob : int
    {
        kSensitivity = 0,
        kDynamics = 1,
        kEq = 2,
        kKnobCount = 3,
    };

    static constexpr float kKnobDefaults[kKnobCount] = { 0.5f, 0.35f, 0.5f };

    void reset()
    {
        for (int i = 0; i < kKnobCount; ++i)
        {
            smoothed[i] = target[i];
        }
        comp.reset();
        swell.reset();
        eq.reset();
        out.reset();
        controlCountdown = 0;
        for (int i = 0; i < kLookahead; ++i)
        {
            delayL[i] = 0.0f;
            delayR[i] = 0.0f;
        }
        delayPos = 0;
        updateControls();
    }

    void setKnob(int idx, float value)
    {
        if (idx >= 0 && idx < kKnobCount && isFiniteF(value))
        {
            target[idx] = clampf(value, 0.0f, 1.0f);
        }
    }

    void setCharacter(Character c) { character = c; }
    Character getCharacter() const { return character; }
    void cycleCharacter()
    {
        character = static_cast<Character>((static_cast<int>(character) + 1) %
                                           static_cast<int>(Character::kCount));
    }

    void setSwell(bool on) { swell.setEnabled(on); }
    bool getSwell() const { return swell.isEnabled(); }

    // Combined gain reduction for metering (compressor + output limiter).
    float meterDb() const { return comp.gainReductionDb() + out.limiterReductionDb(); }

    void process(float* left, float* right, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            if (controlCountdown-- <= 0)
            {
                controlCountdown = kControlInterval - 1;
                updateControls();
            }
            for (int k = 0; k < kKnobCount; ++k)
            {
                smoothed[k] += kKnobSmooth * (target[k] - smoothed[k]);
            }

            float l = left[i];
            float r = right[i];
            if (!isFiniteF(l) || !isFiniteF(r))
            {
                l = 0.0f;
                r = 0.0f;
            }

            const float gain = comp.process(l, r);
            const float sw = swell.process(comp.inputPeakLin(), comp.inputLevelDb(), comp.thresholdDb());
            eq.trackDynamics(comp.inputLevelDb(), comp.thresholdDb());

            // Lookahead delay: write the new sample, read the one from
            // kLookahead samples ago.
            const float dl = delayL[delayPos];
            const float dr = delayR[delayPos];
            delayL[delayPos] = l;
            delayR[delayPos] = r;
            delayPos = delayPos + 1 < kLookahead ? delayPos + 1 : 0;

            l = dl * gain * sw;
            r = dr * gain * sw;
            eq.process(l, r);
            out.process(l, r);

            if (!isFiniteF(l) || !isFiniteF(r))
            {
                // Something blew up: wipe state rather than emit garbage forever.
                reset();
                l = 0.0f;
                r = 0.0f;
            }
            left[i] = l;
            right[i] = r;
        }
    }

  private:
    // SENSITIVITY: higher = reacts to softer playing (lower threshold).
    static float sensitivityToThresholdDb(float s)
    {
        return lerp(kProfile.thresholdAtMinDb, kProfile.thresholdAtMaxDb, s);
    }

    void updateControls()
    {
        Compressor::Controls c;
        c.thresholdDb = sensitivityToThresholdDb(smoothed[kSensitivity]);
        c.dynamics = smoothed[kDynamics];
        c.character = character;
        comp.updateControls(c);

        eq.setKnob(smoothed[kEq]);
        eq.updateCoefficients();

        out.setDusty(character == Character::kDusty);
        out.setDrive(smoothed[kSensitivity]);
    }

    static constexpr int kControlInterval = 16;                     // 3 kHz control rate
    static constexpr int kLookahead = 72;                           // 1.5 ms at 48 kHz
    static constexpr float kKnobSmooth = 1.0f / (0.02f * kSampleRate); // ~20 ms

    Compressor comp;
    Swell swell;
    DynamicEq eq;
    OutputStage out;

    Character character = Character::kStable;
    float target[kKnobCount] = { kKnobDefaults[0], kKnobDefaults[1], kKnobDefaults[2] };
    float smoothed[kKnobCount] = { kKnobDefaults[0], kKnobDefaults[1], kKnobDefaults[2] };
    int controlCountdown = 0;
    float delayL[kLookahead] = {};
    float delayR[kLookahead] = {};
    int delayPos = 0;
};

} // namespace minty
