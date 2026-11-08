#pragma once

// Stage 1 of Minty: the shape-shifting compressor.
//
// One DYNAMICS macro walks through three behaviours, the same order Clean uses:
//   0.00 - 0.50  feedback compression, 1:1 up to "infinite" at noon
//   0.50 - 0.80  feedforward limiting blended in (tighter, snappier)
//   0.80 - 1.00  sag: over-compression plus a drooping, sputtering "supply"
//
// The detector is stereo-linked (max of L/R) and highpassed so bass notes
// don't pump the whole signal. Gain is computed in dB and shared by both
// channels.

#include "DspMath.h"
#include "Filters.h"
#include "InputProfile.h"

namespace minty
{

enum class Character : int
{
    kStable = 0,  // no physics
    kWobble = 1,  // soft spring on the envelope
    kTwitchy = 2, // stiff, under-damped spring with random kicks
    kDusty = 3,   // stable compressor, clipping output stage
    kCount = 4,
};

class Compressor
{
  public:
    struct Controls
    {
        float thresholdDb = -30.0f; // from SENSITIVITY
        float dynamics = 0.35f;     // DYNAMICS macro, 0..1
        Character character = Character::kStable;
    };

    void reset()
    {
        detHpL.setCutoff(kProfile.detectorHighpassHz);
        detHpR.setCutoff(kProfile.detectorHighpassHz);
        peakRelease = 1.0f - timeToCoef(kProfile.peakReleaseSec);
        meanSqCoef = timeToCoef(kProfile.rmsSec);
        detHpL.reset();
        detHpR.reset();
        ffPeak = 0.0f;
        meanSq = 0.0f;
        meanSq2 = 0.0f;
        grDb = 0.0f;
        fatigue = 0.0f;
        springPos = 0.0f;
        springVel = 0.0f;
        supply = 1.0f;
        sputterLp.setCutoff(22.0f);
        sputterLp.reset();
        gateDb = 0.0f;
        lastGain = 1.0f;
        inLevelDb = -180.0f;
        appliedGrDb = 0.0f;
        updateControls(controls);
    }

    // Called at control rate (every few samples). Turns the macro into the
    // per-sample constants used by process().
    void updateControls(const Controls& c)
    {
        controls = c;
        const float d = clampf(c.dynamics, 0.0f, 1.0f);

        // Region A: slope s = 1 - 1/ratio, linear in the first half of the knob.
        // Noon lands on (effectively) infinite ratio.
        const float s = clampf(d / 0.5f, 0.0f, 1.0f);

        ffAmount = smoothstep(0.5f, 0.8f, d);
        sagAmount = clampf((d - 0.8f) / 0.2f, 0.0f, 1.0f);

        // Attack tightens as feedforward limiting takes over.
        attackSec = lerp(0.004f, 0.0008f, ffAmount);
        attackCoef = timeToCoef(attackSec);

        // Feedback loop gain k gives a static ratio of (1 + k). Cap it so the
        // discrete-time loop can't ring: attackCoef * (1 + k) stays < 0.5.
        const float kWanted = s >= 0.999f ? kMaxFeedback : s / (1.0f - s);
        const float kStable = 0.5f / attackCoef - 1.0f;
        fbK = clampf(kWanted, 0.0f, kStable < kMaxFeedback ? kStable : kMaxFeedback);

        // Sag pushes the feedforward slope past 1: louder playing gets quieter.
        ffSlope = 1.0f + 0.6f * sagAmount;

        // Auto make-up gain from the static curve at a hot-ish guitar level.
        const float fbSlope = fbK / (1.0f + fbK);
        const float slope = lerp(fbSlope, ffSlope, ffAmount);
        const float over = softOver(kProfile.makeupRefDb, c.thresholdDb, kKneeDb);
        // Sag deliberately loses level as the supply droops; give some back so
        // the top of the knob isn't a volume cliff.
        makeupDb = clampf(0.8f * slope * over + 10.0f * sagAmount, 0.0f, 32.0f);

        switch (c.character)
        {
            case Character::kWobble:
                springOmega = 2.0f * kPi * 4.5f;
                springZeta = 0.14f;
                springDepth = 1.0f;
                springKick = 0.0f;
                break;
            case Character::kTwitchy:
                springOmega = 2.0f * kPi * 12.0f;
                springZeta = 0.05f;
                springDepth = 1.3f;
                springKick = 1.0f;
                break;
            case Character::kStable:
            case Character::kDusty:
            case Character::kCount:
                springOmega = 2.0f * kPi * 8.0f;
                springZeta = 1.0f;
                springDepth = 0.0f;
                springKick = 0.0f;
                break;
        }
    }

    // Returns the linear gain to apply to both channels for this sample.
    float process(float inL, float inR)
    {
        // --- detection -------------------------------------------------------
        const float hl = detHpL.process(inL);
        const float hr = detHpR.process(inR);
        const float det = std::fabs(hl) > std::fabs(hr) ? std::fabs(hl) : std::fabs(hr);

        // Peak detectors: instant attack, short release. The gain smoother
        // below does the musical attack/release.
        ffPeak = det > ffPeak ? det : ffPeak * peakRelease;
        inLevelDb = linToDb(ffPeak);

        // Mean-square detector for the compression region: smoother and less
        // twitchy about pick transients than the peak detector. +3 dB lines a
        // sine's RMS up with its peak so the threshold means the same thing
        // in both regions.
        meanSq += meanSqCoef * (det * det - meanSq);
        meanSq2 += meanSqCoef * (meanSq - meanSq2);
        const float rmsDb = 4.34294481903f * std::log(meanSq2 > 1.0e-18f ? meanSq2 : 1.0e-18f) + 3.0103f;
        // Peak guard: a pick transient far above the running RMS still gets
        // caught instead of sailing through with full make-up gain on it.
        const float guardedRmsDb = rmsDb > inLevelDb - kPeakGuardDb ? rmsDb : inLevelDb - kPeakGuardDb;
        const float compLevelDb = lerp(guardedRmsDb, inLevelDb, ffAmount);

        // Feedback path, evaluated in dB: the level the detector would see
        // after the current gain reduction. Doing it this way (rather than
        // peak-detecting the gained signal) keeps the detector's hold time
        // out of the loop, so a sudden loud note can't wind the gain up.
        const float fbLevelDb = compLevelDb - grDb;

        // --- gain computer ---------------------------------------------------
        const float fbGr = fbK * softOver(fbLevelDb, controls.thresholdDb, kKneeDb);
        const float ffGr = ffSlope * softOver(compLevelDb, controls.thresholdDb, kKneeDb);
        const float targetGr = clampf(lerp(fbGr, ffGr, ffAmount), 0.0f, kMaxGrDb);

        // --- adaptive envelope -----------------------------------------------
        // "Fatigue" tracks how hard and how long we've been compressing.
        // Staccato playing keeps it low (fast release, lively); sustained
        // squashing raises it (slow release, smooth). Same idea as Clean's
        // Combo envelope: analog attack, adaptive release.
        const float fatigueTarget = clampf(grDb / 15.0f, 0.0f, 1.0f);
        fatigue += (fatigueTarget > fatigue ? kFatigueRise : kFatigueFall) * (fatigueTarget - fatigue);
        const float releaseSec = lerp(0.07f, 1.2f, std::sqrt(fatigue));

        const float coef = targetGr > grDb ? attackCoef : timeToCoefFast(releaseSec);
        grDb += coef * (targetGr - grDb);

        // --- physics: a damped spring chasing the envelope -------------------
        float shapedGr = grDb;
        if (springDepth > 0.0f)
        {
            constexpr float dt = 1.0f / kSampleRate;
            float force = springOmega * springOmega * (grDb - springPos) -
                          2.0f * springZeta * springOmega * springVel;
            if (springKick > 0.0f)
            {
                // Random nudges scaled by how much we're compressing.
                force += springKick * noise.next() * grDb * 4000.0f;
            }
            springVel += force * dt;
            springPos += springVel * dt;
            springPos = clampf(springPos, -12.0f, 60.0f);
            shapedGr = grDb + springDepth * (springPos - grDb);
            // The spring may wobble around the envelope, but it may not lag a
            // fresh pick by more than a few dB, or every attack would punch
            // straight through to the limiter.
            if (shapedGr < grDb - kSpringMaxLagDb)
            {
                shapedGr = grDb - kSpringMaxLagDb;
            }
        }

        // --- sag: a tired power supply that droops and sputters --------------
        float sagGr = 0.0f;
        if (sagAmount > 0.0f)
        {
            const float overNorm = clampf((inLevelDb - controls.thresholdDb) / 24.0f, 0.0f, 1.0f);
            const float drain = sagAmount * overNorm * kSagDrainPerSample * supply;
            const float recover = (1.0f - supply) * kSagRecoverPerSample;
            supply = clampf(supply - drain + recover, 0.25f, 1.0f);

            const float sputter = sputterLp.process(noise.next()) * 3.0f;
            sagGr = -linToDb(supply) * (1.0f + sagAmount) +
                    sagAmount * (1.0f - supply) * 22.0f * sputter;
        }
        else
        {
            supply = 1.0f;
        }

        // --- downward expander / gate on the input ---------------------------
        // Compression lifts the noise floor; this keeps the gaps quiet.
        const float gateTarget = clampf((inLevelDb - kProfile.gateThresholdDb) * 2.0f, -40.0f, 0.0f);
        gateDb += (gateTarget > gateDb ? kGateOpen : kGateClose) * (gateTarget - gateDb);

        appliedGrDb = clampf(shapedGr + sagGr, -3.0f, 60.0f);
        // Note the feedback path above reads grDb, not appliedGrDb. If it saw
        // the spring, the spring would sit inside the loop and self-oscillate
        // on a sustained note instead of settling.
        lastGain = dbToLin(makeupDb + gateDb - appliedGrDb);
        return lastGain;
    }

    float gainReductionDb() const { return appliedGrDb; }
    float inputLevelDb() const { return inLevelDb; }
    float inputPeakLin() const { return ffPeak; }
    float thresholdDb() const { return controls.thresholdDb; }

  private:
    static constexpr float kKneeDb = 6.0f;
    static constexpr float kPeakGuardDb = 8.0f;
    static constexpr float kSpringMaxLagDb = 4.0f;
    static constexpr float kMaxFeedback = 40.0f;
    static constexpr float kMaxGrDb = 48.0f;

    // Precomputed one-pole coefficients (48 kHz).
    static constexpr float kFatigueRise = 1.39e-5f;   // ~1.5 s
    static constexpr float kFatigueFall = 6.94e-6f;   // ~3 s
    static constexpr float kGateOpen = 0.0206f;       // ~1 ms
    static constexpr float kGateClose = 1.74e-4f;     // ~120 ms
    static constexpr float kSagDrainPerSample = 1.6e-4f;   // droops in ~130 ms when slammed
    static constexpr float kSagRecoverPerSample = 8.3e-5f; // recovers in ~250 ms

    Controls controls{};

    OnePoleHighpass detHpL, detHpR;
    OnePoleLowpass sputterLp;
    Noise noise;

    float ffPeak = 0.0f, meanSq = 0.0f, meanSq2 = 0.0f;
    float peakRelease = 0.999f, meanSqCoef = 0.002f; // set from kProfile in reset()
    float inLevelDb = -180.0f;
    float grDb = 0.0f, appliedGrDb = 0.0f;
    float fatigue = 0.0f;
    float springPos = 0.0f, springVel = 0.0f;
    float supply = 1.0f;
    float gateDb = 0.0f;
    float lastGain = 1.0f;

    // Derived from controls.
    float ffAmount = 0.0f, sagAmount = 0.0f;
    float attackSec = 0.006f, attackCoef = 0.0035f;
    float fbK = 1.0f, ffSlope = 1.0f, makeupDb = 0.0f;
    float springOmega = 0.0f, springZeta = 1.0f, springDepth = 0.0f, springKick = 0.0f;
};

} // namespace minty
