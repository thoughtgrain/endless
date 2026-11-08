#pragma once

// Every constant that depends on what's plugged into the Endless lives here,
// so retuning Minty for a different source is one struct, not a hunt.
//
// Synth is the default. Build with -DMINTY_PROFILE_GUITAR (e.g.
// make CUSTOM_COMPILER_OPTIONS=-DMINTY_PROFILE_GUITAR) for the guitar tuning.

namespace minty
{

struct InputProfile
{
    // SENSITIVITY knob sweep: threshold at fully CCW / fully CW, dBFS.
    float thresholdAtMinDb;
    float thresholdAtMaxDb;

    // Auto make-up gain is computed from the static curve at this level.
    float makeupRefDb;

    // Detector highpass. Higher = bass pumps less, but bass also gets less
    // control and leans on the limiter more.
    float detectorHighpassHz;

    // RMS detector: two cascaded one-poles of this time constant. Cascading
    // gives 12 dB/oct of ripple rejection, which is what keeps sub-bass from
    // modulating its own gain.
    float rmsSec;

    // Peak detector release (limiter region, gate, swell, EQ shift).
    float peakReleaseSec;

    // Downward expander threshold.
    float gateThresholdDb;

    // Dusty drive range in dB across the SENSITIVITY knob.
    float dustyDriveMinDb;
    float dustyDriveMaxDb;

    // Dusty output trim, level-matched against Stable on each profile's test phrase.
    float dustyLevel;

    // Swell re-triggers on each new attack (hides guitar picks). Off for
    // synth: on a fast sequence it would duck every note and never open up,
    // so the swell happens once per phrase instead.
    bool swellRetrigger;
};

// Guitar: instrument level, peaky pick attacks, little below 80 Hz.
constexpr InputProfile kGuitarProfile{
    .thresholdAtMinDb = -6.0f,
    .thresholdAtMaxDb = -54.0f,
    .makeupRefDb = -16.0f,
    .detectorHighpassHz = 90.0f,
    .rmsSec = 0.008f,
    .peakReleaseSec = 0.015f,
    .gateThresholdDb = -68.0f,
    .dustyDriveMinDb = 3.0f,
    .dustyDriveMaxDb = 27.0f,
    .dustyLevel = 0.62f,
    .swellRetrigger = true,
};

// Synth: line level that often sits near full scale, sustained notes with low
// crest factor, real content down to 30-40 Hz.
constexpr InputProfile kSynthProfile{
    .thresholdAtMinDb = -3.0f,
    .thresholdAtMaxDb = -45.0f,
    .makeupRefDb = -9.0f,
    .detectorHighpassHz = 30.0f,
    .rmsSec = 0.012f,
    .peakReleaseSec = 0.045f,
    .gateThresholdDb = -74.0f,
    .dustyDriveMinDb = 0.0f,
    .dustyDriveMaxDb = 20.0f,
    .dustyLevel = 0.98f,
    .swellRetrigger = false,
};

#ifdef MINTY_PROFILE_GUITAR
constexpr const InputProfile& kProfile = kGuitarProfile;
#else
constexpr const InputProfile& kProfile = kSynthProfile;
#endif

} // namespace minty
