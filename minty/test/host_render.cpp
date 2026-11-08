// Host-side test bench for the Minty engine.
//   make host-test   (builds with the system g++, runs, writes WAVs to build/host/)
//
// Checks: static compression curves, loop stability, physics depth, safety
// under abuse (NaN, DC, full-scale square, knob slams), and renders a
// synthetic guitar phrase through a handful of settings for listening.

#include "../source/minty/MintyEngine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using minty::Character;
using minty::MintyEngine;

namespace
{
constexpr int kFs = 48000;
constexpr int kBlock = 32;
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void runEngine(MintyEngine& e, std::vector<float>& l, std::vector<float>& r)
{
    for (size_t i = 0; i < l.size(); i += kBlock)
    {
        const int n = static_cast<int>(l.size() - i < kBlock ? l.size() - i : kBlock);
        e.process(&l[i], &r[i], n);
    }
}

float rmsDb(const std::vector<float>& x, size_t from, size_t to)
{
    double acc = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    const double meanSq = acc / static_cast<double>(to - from);
    return 10.0f * std::log10(static_cast<float>(meanSq) + 1e-20f);
}

void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path, "wb");
    if (!f)
    {
        std::printf("  could not write %s\n", path);
        return;
    }
    const uint32_t frames = static_cast<uint32_t>(l.size());
    const uint32_t dataBytes = frames * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(kFs); u32(kFs * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        int16_t s[2] = { static_cast<int16_t>(minty::clampf(l[i], -1.0f, 1.0f) * 32767.0f),
                         static_cast<int16_t>(minty::clampf(r[i], -1.0f, 1.0f) * 32767.0f) };
        std::fwrite(s, 2, 2, f);
    }
    std::fclose(f);
}

// Karplus-Strong plucked string, good enough to exercise a guitar compressor.
void pluck(std::vector<float>& out, size_t start, float hz, float velocity, float seconds, uint32_t seed)
{
    const int period = static_cast<int>(kFs / hz);
    std::vector<float> ring(static_cast<size_t>(period));
    minty::Noise n;
    n.state = seed;
    for (auto& s : ring)
    {
        s = n.next() * velocity;
    }
    // Soften the initial burst a little, like a pick rather than a razor.
    float prev = 0.0f;
    for (auto& s : ring)
    {
        prev += 0.6f * (s - prev);
        s = prev;
    }
    const size_t len = static_cast<size_t>(seconds * kFs);
    int idx = 0;
    for (size_t i = 0; i < len && start + i < out.size(); ++i)
    {
        const int nxt = (idx + 1) % period;
        const float v = ring[static_cast<size_t>(idx)];
        ring[static_cast<size_t>(idx)] = 0.4985f * (v + ring[static_cast<size_t>(nxt)]);
        const float fade = i + 2400 > len ? static_cast<float>(len - i) / 2400.0f : 1.0f;
        out[start + i] += v * fade;
        idx = nxt;
    }
}

[[maybe_unused]] std::vector<float> guitarPhrase()
{
    std::vector<float> x(static_cast<size_t>(kFs * 9), 0.0f);
    const float notes[] = { 196.0f, 246.9f, 293.7f, 392.0f, 293.7f, 246.9f, 220.0f, 164.8f };
    const float vel[] = { 0.15f, 0.35f, 0.08f, 0.6f, 0.2f, 0.45f, 0.05f, 0.7f };
    for (int i = 0; i < 8; ++i)
    {
        pluck(x, static_cast<size_t>(i * 0.45f * kFs), notes[i], vel[i], 1.2f, 1000u + static_cast<uint32_t>(i));
    }
    // A soft chord, then a hard strum, then a long ringing note.
    const float chord[] = { 82.4f, 123.5f, 164.8f, 207.7f, 246.9f, 329.6f };
    for (int s = 0; s < 6; ++s)
    {
        pluck(x, static_cast<size_t>(4.0f * kFs) + static_cast<size_t>(s * 600), chord[s], 0.12f, 1.4f, 2000u + static_cast<uint32_t>(s));
        pluck(x, static_cast<size_t>(5.6f * kFs) + static_cast<size_t>(s * 250), chord[s], 0.55f, 1.6f, 3000u + static_cast<uint32_t>(s));
    }
    pluck(x, static_cast<size_t>(7.3f * kFs), 110.0f, 0.5f, 1.7f, 4242u);
    float peak = 0.0f;
    for (float v : x)
    {
        peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    for (float& v : x)
    {
        v *= 0.7f / peak; // hot but not clipping, roughly -3 dBFS peaks
    }
    return x;
}

// Naive oscillators + envelopes: enough to look like a synth to a compressor.
// Sub-bass sequence, a slow pad, a plucky lead, then hard chord stabs.
// Normalised to -1 dBFS peaks because line-level synths run hot.
[[maybe_unused]] std::vector<float> synthPhrase()
{
    std::vector<float> x(static_cast<size_t>(kFs * 11), 0.0f);
    auto saw = [](double ph) { return static_cast<float>(2.0 * (ph - std::floor(ph)) - 1.0); };
    auto sqr = [](double ph) { return (ph - std::floor(ph)) < 0.5 ? 1.0f : -1.0f; };

    // 0-3 s: sub/bass line, 8th notes at 120 BPM, lowpassed saw.
    const float bass[] = { 41.2f, 41.2f, 55.0f, 49.0f, 36.7f, 36.7f, 49.0f, 55.0f, 41.2f, 61.7f, 55.0f, 36.7f };
    const float bassVel[] = { 1.0f, 0.5f, 0.8f, 0.6f, 1.0f, 0.4f, 0.7f, 0.9f, 1.0f, 0.5f, 0.8f, 1.0f };
    for (int nIdx = 0; nIdx < 12; ++nIdx)
    {
        const size_t start = static_cast<size_t>(nIdx * 0.25f * kFs);
        float lp = 0.0f;
        for (size_t i = 0; i < static_cast<size_t>(0.24f * kFs); ++i)
        {
            const float t = static_cast<float>(i) / kFs;
            const float env = (t < 0.003f ? t / 0.003f : 1.0f) * std::exp(-t * 6.0f);
            lp += 0.03f * (saw(static_cast<double>(bass[nIdx]) * static_cast<double>(t)) - lp);
            x[start + i] += lp * env * bassVel[nIdx];
        }
    }
    // 3-6 s: pad, 4 detuned saws per note, 400 ms attack, 600 ms release.
    const float pad[] = { 130.8f, 155.6f, 196.0f, 233.1f };
    float padLp = 0.0f;
    for (size_t i = 0; i < static_cast<size_t>(3.0f * kFs); ++i)
    {
        const float t = static_cast<float>(i) / kFs;
        const float env = t < 0.4f ? t / 0.4f : (t > 2.4f ? (3.0f - t) / 0.6f : 1.0f);
        float v = 0.0f;
        for (float f : pad)
        {
            v += saw(static_cast<double>(f) * static_cast<double>(t)) + saw(static_cast<double>(f * 1.006f) * static_cast<double>(t));
        }
        padLp += 0.12f * (v - padLp);
        x[static_cast<size_t>(3.0f * kFs) + i] += padLp * env * 0.12f;
    }
    // 6-9 s: plucky square lead, 16ths, uneven velocity.
    const float lead[] = { 523.3f, 659.3f, 784.0f, 659.3f, 587.3f, 784.0f, 880.0f, 523.3f };
    for (int nIdx = 0; nIdx < 24; ++nIdx)
    {
        const size_t start = static_cast<size_t>((6.0f + nIdx * 0.125f) * kFs);
        const float vel = 0.25f + 0.75f * static_cast<float>((nIdx * 7) % 5) / 4.0f;
        for (size_t i = 0; i < static_cast<size_t>(0.12f * kFs); ++i)
        {
            const float t = static_cast<float>(i) / kFs;
            x[start + i] += sqr(static_cast<double>(lead[nIdx % 8]) * static_cast<double>(t)) * std::exp(-t * 30.0f) * vel * 0.5f;
        }
    }
    // 9-11 s: four hard stabs.
    for (int k = 0; k < 4; ++k)
    {
        const size_t start = static_cast<size_t>((9.0f + k * 0.5f) * kFs);
        for (size_t i = 0; i < static_cast<size_t>(0.18f * kFs); ++i)
        {
            const float t = static_cast<float>(i) / kFs;
            float v = 0.0f;
            for (float f : pad)
            {
                v += saw(static_cast<double>(f * 2.0f) * static_cast<double>(t));
            }
            x[start + i] += v * 0.3f * std::exp(-t * 8.0f);
        }
    }
    float peak = 0.0f;
    for (float v : x)
    {
        peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    for (float& v : x)
    {
        v *= 0.89f / peak;
    }
    return x;
}

#ifdef MINTY_PROFILE_GUITAR
constexpr const char* kProfileName = "guitar";
std::vector<float> testPhrase() { return guitarPhrase(); }
#else
constexpr const char* kProfileName = "synth";
std::vector<float> testPhrase() { return synthPhrase(); }
#endif

MintyEngine makeEngine(float sens, float dyn, float eq, Character c, bool swellOn)
{
    MintyEngine e;
    e.setKnob(0, sens);
    e.setKnob(1, dyn);
    e.setKnob(2, eq);
    e.setCharacter(c);
    e.setSwell(swellOn);
    e.reset();
    return e;
}

void staticCurves()
{
    std::printf("\nStatic curves (1 kHz sine, SENSITIVITY 0.5 -> threshold %.1f dBFS, RMS dB)\n",
                static_cast<double>(minty::lerp(minty::kProfile.thresholdAtMinDb, minty::kProfile.thresholdAtMaxDb, 0.5f)));
    std::printf("  dyn  |");
    for (int lvl = -54; lvl <= -6; lvl += 6)
    {
        std::printf(" %5d", lvl);
    }
    std::printf("   slope above thr\n");
    const float dyns[] = { 0.0f, 0.12f, 0.25f, 0.4f, 0.5f, 0.65f, 0.8f, 0.9f, 1.0f };
    for (float d : dyns)
    {
        std::printf("  %.2f |", static_cast<double>(d));
        float outAtM18 = 0.0f, outAt0 = 0.0f;
        for (int lvl = -54; lvl <= -6; lvl += 6)
        {
            MintyEngine e = makeEngine(0.5f, d, 0.5f, Character::kStable, false);
            const size_t n = kFs * 2;
            std::vector<float> l(n), r(n);
            const float amp = minty::dbToLin(static_cast<float>(lvl) + 3.0103f); // RMS -> peak
            for (size_t i = 0; i < n; ++i)
            {
                l[i] = r[i] = amp * std::sin(2.0f * minty::kPi * 1000.0f * static_cast<float>(i) / kFs);
            }
            runEngine(e, l, r);
            const float o = rmsDb(l, n - kFs / 4, n);
            if (lvl == -18) outAtM18 = o;
            if (lvl == -6) outAt0 = o;
            std::printf(" %5.1f", static_cast<double>(o));
        }
        const float slope = (outAt0 - outAtM18) / 12.0f;
        std::printf("   %.2f (~%s)\n", static_cast<double>(slope),
                    slope < 0.0f ? "negative/sag" : (slope < 0.05f ? "inf:1" : "ratio"));
        if (d <= 0.001f)
        {
            check(std::fabs(slope - 1.0f) < 0.1f, "dyn 0 should be ~1:1");
        }
        if (std::fabs(d - 0.5f) < 0.01f)
        {
            check(slope < 0.12f, "noon should be near-infinite ratio");
        }
    }
}

void stabilityAndPhysics()
{
    std::printf("\nStep response (tone jumps -40 -> -6 dBFS), gain-reduction ripple after settling\n");
    struct Case { const char* name; float dyn; Character c; };
    const Case cases[] = {
        { "noon / stable", 0.5f, Character::kStable },
        { "0.65 / stable", 0.65f, Character::kStable },
        { "0.40 / wobble", 0.4f, Character::kWobble },
        { "0.40 / twitchy", 0.4f, Character::kTwitchy },
    };
    for (const auto& cs : cases)
    {
        MintyEngine e = makeEngine(0.5f, cs.dyn, 0.5f, cs.c, false);
        const size_t n = kFs * 3;
        std::vector<float> l(n), r(n);
        float grMin = 1e9f, grMax = -1e9f;
        for (size_t i = 0; i < n; i += kBlock)
        {
            for (size_t k = i; k < i + kBlock; ++k)
            {
                const float amp = k < kFs ? 0.01f : 0.5f;
                l[k] = r[k] = amp * std::sin(2.0f * minty::kPi * 220.0f * static_cast<float>(k) / kFs);
            }
            e.process(&l[i], &r[i], kBlock);
            if (i > kFs * 2)
            {
                const float m = e.meterDb();
                grMin = m < grMin ? m : grMin;
                grMax = m > grMax ? m : grMax;
            }
        }
        std::printf("  %-16s GR %.1f .. %.1f dB (ripple %.2f dB)\n", cs.name,
                    static_cast<double>(grMin), static_cast<double>(grMax),
                    static_cast<double>(grMax - grMin));
        if (cs.c == Character::kStable)
        {
            check(grMax - grMin < 1.5f, "stable character should settle without ringing");
        }
    }
}

// Harmonic distortion of a sustained low sine: this is what tells you whether
// the detector is ripple-modulating the gain on bass notes.
float thdPercent(float hz, float dbfs, float dyn)
{
    MintyEngine e = makeEngine(0.5f, dyn, 0.5f, Character::kStable, false);
    const size_t n = kFs * 2;
    std::vector<float> l(n), r(n);
    const float a = minty::dbToLin(dbfs);
    for (size_t i = 0; i < n; ++i)
    {
        l[i] = r[i] = a * std::sin(2.0f * minty::kPi * hz * static_cast<float>(i) / kFs);
    }
    runEngine(e, l, r);
    const size_t N = kFs / 2, s0 = n - N;
    double p[9] = {};
    for (int h = 1; h <= 8; ++h)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < N; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * static_cast<double>(i) / static_cast<double>(N));
            const double w = 2.0 * 3.14159265358979 * static_cast<double>(hz) * h * static_cast<double>(s0 + i) / kFs;
            re += hann * static_cast<double>(l[s0 + i]) * std::cos(w);
            im += hann * static_cast<double>(l[s0 + i]) * std::sin(w);
        }
        p[h] = re * re + im * im;
    }
    double harm = 0.0;
    for (int h = 2; h <= 8; ++h)
    {
        harm += p[h];
    }
    return static_cast<float>(100.0 * std::sqrt(harm / p[1]));
}

void bassDistortion()
{
    std::printf("\nBass THD %% (sine at -6 dBFS, SENSITIVITY 0.5)\n  dyn:         0.25    0.50    0.70\n");
    const float freqs[] = { 30.0f, 40.0f, 55.0f, 110.0f };
    const float dyns[] = { 0.25f, 0.5f, 0.7f };
    for (float hz : freqs)
    {
        std::printf("  %5.0f Hz  ", static_cast<double>(hz));
        for (float d : dyns)
        {
            const float t = thdPercent(hz, -6.0f, d);
            std::printf("%7.2f ", static_cast<double>(t));
            check(t < 0.5f, "bass THD should stay under 0.5 %");
        }
        std::printf("\n");
    }
}

void abuse()
{
    std::printf("\nAbuse test (all characters x knob extremes x swell, NaN/DC/square/noise inputs)\n");
    float worst = 0.0f;
    bool allFinite = true;
    const float ks[] = { 0.0f, 0.5f, 1.0f };
    minty::Noise nz;
    for (int c = 0; c < static_cast<int>(Character::kCount); ++c)
        for (float s : ks)
            for (float d : ks)
                for (float q : ks)
                    for (int sw = 0; sw < 2; ++sw)
                    {
                        MintyEngine e = makeEngine(s, d, q, static_cast<Character>(c), sw != 0);
                        const size_t n = kFs / 2;
                        std::vector<float> l(n), r(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            const int phase = static_cast<int>(i / (n / 5));
                            float v = 0.0f;
                            switch (phase)
                            {
                                case 0: v = (i / 50) % 2 ? 1.0f : -1.0f; break;       // full-scale square
                                case 1: v = nz.next() * 4.0f; break;                    // over-range noise
                                case 2: v = 1.0f; break;                                // DC
                                case 3: v = (i % 997 == 0) ? NAN : nz.next() * 0.3f; break; // NaN sprinkles
                                default: v = 1e-30f; break;                             // denormal-ish
                            }
                            l[i] = v;
                            r[i] = -v;
                        }
                        // Slam knobs mid-run.
                        for (size_t i = 0; i < n; i += kBlock)
                        {
                            if ((i / kBlock) % 200 == 0)
                            {
                                e.setKnob(static_cast<int>((i / kBlock) % 3), nz.next() * 0.5f + 0.5f);
                            }
                            e.process(&l[i], &r[i], kBlock);
                        }
                        for (size_t i = 0; i < n; ++i)
                        {
                            allFinite = allFinite && std::isfinite(l[i]) && std::isfinite(r[i]);
                            const float a = std::fabs(l[i]) > std::fabs(r[i]) ? std::fabs(l[i]) : std::fabs(r[i]);
                            worst = a > worst ? a : worst;
                        }
                    }
    std::printf("  worst |out| = %.4f, all finite = %s\n", static_cast<double>(worst), allFinite ? "yes" : "NO");
    check(allFinite, "output must stay finite");
    check(worst < 1.0f, "output must stay inside (-1, 1)");
}

void renders()
{
    std::printf("\nRendering %s phrase -> build/host/*.wav\n", kProfileName);
    const std::vector<float> dry = testPhrase();
    writeWav("build/host/00_dry.wav", dry, dry);

    struct Setting { const char* file; float s, d, q; Character c; bool sw; };
    const Setting settings[] = {
        { "build/host/01_gentle.wav", 0.45f, 0.22f, 0.5f, Character::kStable, false },
        { "build/host/02_squash_noon.wav", 0.6f, 0.5f, 0.5f, Character::kStable, false },
        { "build/host/03_sag.wav", 0.6f, 0.95f, 0.5f, Character::kStable, false },
        { "build/host/04_wobble.wav", 0.6f, 0.4f, 0.5f, Character::kWobble, false },
        { "build/host/05_twitchy.wav", 0.6f, 0.4f, 0.5f, Character::kTwitchy, false },
        { "build/host/06_dusty.wav", 0.7f, 0.3f, 0.5f, Character::kDusty, false },
        { "build/host/07_swell.wav", 0.55f, 0.35f, 0.5f, Character::kStable, true },
        { "build/host/08_dark_shifty_eq.wav", 0.55f, 0.35f, 0.12f, Character::kStable, false },
    };
    std::printf("  %-34s %8s %8s %8s\n", "file", "peak", "rms dB", "crest");
    const float dryRms = rmsDb(dry, 0, dry.size());
    float dryPeak = 0.0f;
    for (float v : dry) dryPeak = std::fabs(v) > dryPeak ? std::fabs(v) : dryPeak;
    std::printf("  %-34s %8.3f %8.1f %8.1f\n", "dry", static_cast<double>(dryPeak), static_cast<double>(dryRms),
                static_cast<double>(minty::linToDb(dryPeak) - dryRms));
    for (const auto& st : settings)
    {
        MintyEngine e = makeEngine(st.s, st.d, st.q, st.c, st.sw);
        std::vector<float> l = dry, r = dry;
        runEngine(e, l, r);
        writeWav(st.file, l, r);
        float peak = 0.0f;
        for (float v : l) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        const float rms = rmsDb(l, 0, l.size());
        std::printf("  %-34s %8.3f %8.1f %8.1f\n", st.file + 11, static_cast<double>(peak),
                    static_cast<double>(rms), static_cast<double>(minty::linToDb(peak) - rms));
    }
}

void timing()
{
    MintyEngine e = makeEngine(0.6f, 0.9f, 0.3f, Character::kTwitchy, true);
    std::vector<float> l = testPhrase(), r = l;
    const auto t0 = std::chrono::steady_clock::now();
    for (int rep = 0; rep < 5; ++rep)
    {
        runEngine(e, l, r);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double audioSec = static_cast<double>(5 * l.size()) / static_cast<double>(kFs);
    std::printf("\nHost speed (worst-case settings): %.0fx realtime on this machine\n", audioSec / sec);
}
} // namespace

int main()
{
    std::printf("Minty host test bench (%s profile)\n", kProfileName);
    staticCurves();
    stabilityAndPhysics();
    bassDistortion();
    abuse();
    renders();
    timing();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
