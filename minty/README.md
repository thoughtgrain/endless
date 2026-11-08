# Minty

Minty is a creative compressor patch for the Polyend Endless, built with the FxPatch SDK. It's my take on the Chase Bliss Clean: a two-stage compressor that can go from barely-there leveling to brick-wall limiting to a tired, sagging tube amp. It also has a dynamic EQ, a physics-style wobble, a crumbly overdrive mode and a dynamic swell.

Minty is tuned for **synths** by default (line level, sustained notes, real sub-bass). The original guitar tuning is still there behind one build flag; see [Input profiles](#input-profiles).

It isn't a clone of Clean, and it can't be. Clean has six knobs, three toggles and 16 DIP switches, and an Endless patch gets three knobs and one footswitch. So I kept the behaviours that make Clean feel like Clean, then folded them into macros that fit the hardware.

## Controls

| Control | What it does |
|---|---|
| **Left knob: SENSITIVITY** | Threshold, from -3 dBFS (fully CCW) to -45 dBFS (fully CW) on the synth profile. Turn it up and Minty reacts to softer playing. It also sets the swell trigger point and, in Dusty mode, the drive. |
| **Mid knob: DYNAMICS** | One knob covering compress → limit → sag, the same order Clean uses (details below). Make-up gain is automatic. |
| **Right knob: EQ** | Dynamic "Shifty" EQ. Below noon it darkens (a lowpass closes down to ~550 Hz). Above noon it thins out (a highpass opens up to ~1.4 kHz). Noon is flat, with a small dead zone so a real pot actually finds flat. When you dig in past the threshold, the filter slides back toward full range, then drifts back as you ease off. |
| **Footswitch press** | Dynamic swell on/off. |
| **Footswitch hold** | Cycles the character: **Stable → Wobble → Twitchy → Dusty**. |

### The LED

Colour shows the mode and brightness shows whether the compressor is working (more than 3 dB of gain reduction). That's my stand-in for Clean's compression-meter LED.

| Mode | Idle | Compressing |
|---|---|---|
| Stable | dim green | pastel green (mint) |
| Wobble | dim cyan | light blue |
| Twitchy | dim yellow | light yellow |
| Dusty | dim white | beige |
| Swell on (overrides) | dark cobalt | blue |

I kept red free because the SDK wrapper uses it to report errors.

## How DYNAMICS maps out

I measured all of these with a 1 kHz sine and SENSITIVITY at noon (`make host-test` prints the full table).

| Knob | Behaviour | Measured slope above threshold |
|---|---|---|
| 0.00 | 1:1, transparent | 1.00 |
| 0.25 | feedback compression, ~2:1 | 0.50 |
| 0.40 | ~5:1 | 0.20 |
| 0.50 (noon) | "infinite", basically a leveler | 0.06 |
| 0.50 → 0.80 | feedforward limiting gets blended in. Attack tightens from 4 ms to 0.8 ms | ~0 |
| 0.80 → 1.00 | **sag**: over-compression (louder gets quieter) plus a drooping, sputtering "power supply" | -0.5 to -0.9 |

The first half is a feedback design and the limiting half is feedforward, which is the same split the Clean manual describes. Feedback reads the already-compressed signal, so it's naturally relaxed and program-dependent. Feedforward reads the input directly, so it's precise and snappy.

## Input profiles

Everything that depends on what's plugged in lives in `source/minty/InputProfile.h`, so retuning means changing one struct.

| | Synth (default) | Guitar |
|---|---|---|
| SENSITIVITY sweep | -3 → -45 dBFS | -6 → -54 dBFS |
| Make-up reference level | -9 dBFS | -16 dBFS |
| Detector highpass | 30 Hz | 90 Hz |
| RMS detector | 2 × 12 ms cascaded | 2 × 8 ms cascaded |
| Peak detector release | 45 ms | 15 ms |
| Expander threshold | -74 dBFS | -68 dBFS |
| Dusty drive | 0 → +20 dB | +3 → +27 dB |
| Swell re-trigger per note | off | on |

Here's why the synth numbers moved:

- **The threshold sweep shifts up** because line-level synths sit near full scale. On guitar the bottom of the sweep did nothing for a synth, and the top of it compressed even the noise floor.
- **The detector highpass drops to 30 Hz.** At 90 Hz, a bass line around 40 Hz was mostly invisible to the compressor, so it skipped compression and went straight into the limiter. At 30 Hz the bass gets actual compression.
- **Slower, cascaded detectors keep low notes clean.** Once the compressor can see bass, its detector has to stop following individual cycles of the waveform, or the gain wobbles with it and adds distortion. Two cascaded one-poles roll ripple off at 12 dB/oct, and the longer peak release covers a 30 Hz half-cycle. I measured harmonic distortion on a sustained -6 dBFS sine at SENSITIVITY noon:

  | | 30 Hz | 40 Hz | 55 Hz | 110 Hz |
  |---|---|---|---|---|
  | Synth profile, DYNAMICS noon | 0.16 % | 0.07 % | 0.03 % | <0.01 % |
  | Guitar profile, DYNAMICS noon | 0.27 % | 0.15 % | 0.07 % | 0.01 % |

  So the synth tuning compresses the bass *more* and distorts it *less*. `make host-test` fails if any of these goes over 0.5 %.
- **Swell re-trigger is off.** On guitar, re-triggering on every new attack is what hides the pick. On a 16th-note synth line it ducked every other note and never finished fading in, so the line nearly vanished. On synth, the swell fades in once per phrase and fades out across real gaps.
- **Dusty drive and trim** are rescaled for a hotter input, and level-matched to Stable on the synth test material.

To build the guitar version:

```sh
make CUSTOM_COMPILER_OPTIONS=-DMINTY_PROFILE_GUITAR
```

## Signal flow

```
 in ─┬─► 1.5 ms delay ──► × gain × swell ──► Dynamic EQ ──► Output stage ──► out
     │                       ▲        ▲          ▲            (limiter / Dusty clip)
     └─► detector ──► Compressor ──► Swell ──────┘
         (HPF 30 Hz,   level + threshold also drive the EQ shift
          stereo-linked)
```

There are a few design decisions here worth explaining:

- **Lookahead (1.5 ms).** The detector hears the input 72 samples before the audio path does. Without it, pick attacks came through as single-sample needles carrying full make-up gain, and the swell couldn't hide the pick. 1.5 ms is about the same latency a lot of digital pedals already add.
- **Feedback path evaluated in dB.** I compute the feedback level as `detector level − current gain reduction` instead of peak-detecting the gained signal. My first version peak-detected, and on a sudden loud note the detector's hold time sat inside the loop and wound the gain up to 90 dB of reduction. The dB form is mathematically the same static curve with no lag in the loop. I also cap the loop gain so `attackCoef × (1 + k) < 0.5`, which keeps the discrete-time loop from ringing.
- **RMS detection with a peak guard.** The compression region follows a cascaded RMS detector (time constants per profile) so it's smooth. A transient more than 8 dB above the RMS still gets caught. The limiting region uses the fast peak detector.
- **Adaptive release.** Release slides between 70 ms and 1.2 s depending on a slow "fatigue" measure of how hard and how long it's been compressing. Staccato playing stays lively and long squashed chords stay smooth. That's my version of Clean's "Combo" envelope (analog attack, adaptive release).
- **Detector highpass.** Same idea as Clean's envelope-balance option: it decides how much the lows get to drive the compressor. It's 30 Hz for synth and 90 Hz for guitar.
- **Downward expander** (-74 dBFS synth, -68 guitar). Compression with lots of make-up gain drags the noise floor up, so the gaps between notes get pulled back down.

## Characters (footswitch hold)

- **Stable**: just the compressor.
- **Wobble**: a damped spring (4.5 Hz, ζ = 0.14) chases the compressor envelope. Every change in compression overshoots and settles, so you get a soft breathing wobble after each note.
- **Twitchy**: a stiff, barely damped spring (12 Hz, ζ = 0.05) that gets random kicks scaled by how hard it's compressing. You get flutter and nervous little bursts.
- **Dusty**: the final limiter turns into an asymmetric soft clipper (the asymmetry adds even harmonics, which is the crumbly part). SENSITIVITY doubles as drive (0 to +20 dB on synth). The output is level-matched.

The spring only shapes the gain you hear. The feedback detector never sees it, because if it did, the spring would sit inside the loop and self-oscillate on a sustained note (I tried it, and it did). The spring also isn't allowed to trail a fresh pick attack by more than 4 dB, so attacks don't slam the limiter.

## Swell (footswitch press)

This is Clean's default "Dynamic" swell. Play past the threshold and the volume fades in over 450 ms. Stop and it fades out over 600 ms. On the guitar profile, a fresh pick over a ringing note re-triggers it: a 1.2 ms duck happens inside the lookahead window, then the fade-in starts again, so every note gets the violin treatment. On the synth profile re-trigger is off, so legato lines and sequences swell in once and then play through. The fade uses a squared curve so it feels like a volume pedal rather than a linear fader.

## What I left out, and why

- **Separate WET/DRY (parallel compression).** There's no knob left for it. I went with auto make-up gain instead, tuned so output loudness lands within about 1.5 dB of the dry signal on the synth test phrase (sag is the exception and sits quieter on purpose).
- **Attack/Release knobs and the release toggle.** These are replaced by the DYNAMICS-linked attack and the adaptive release.
- **Manual swell, Motion mode, Spread/stereo-independent processing, sidechain, MIDI, ramping, presets.** The SDK doesn't expose MIDI, a sidechain input, expression or a footswitch release event, so these would be guesses.
- **Momentary swell.** The SDK only sends press and hold events, never a release, so the swell is latching.

## Build

```sh
make TOOLCHAIN=arm-none-eabi-        # → build/minty_<timestamp>.endl
make host-test                        # desktop test bench + WAV renders in build/host/ (synth)
make host-test-guitar                 # same bench, guitar profile
```

Then copy the `.endl` file to the Endless USB drive. Note that the SDK README says `.bin`, but its Makefile actually produces `.endl`.

The firmware build is 16.6 KB, uses 80 bytes of stack in the audio callback, and does no heap allocation (I checked the ELF for malloc/new/sbrk and there are none). It doesn't use the working buffer either.

## Files

```
source/PatchImpl.cpp          Endless glue: knobs, footswitch, LED
source/minty/MintyEngine.h    The full chain, host-independent
source/minty/Compressor.h     Stage 1: detector, FB/FF gain computer, adaptive envelope, physics, sag, expander
source/minty/Swell.h          Dynamic swell with pick re-trigger
source/minty/DynamicEq.h      Shifty dynamic EQ (TPT SVF lowpass + highpass)
source/minty/OutputStage.h    Stage 2: peak limiter or Dusty clipper, plus safety ceiling
source/minty/Filters.h        One-pole HP/LP, TPT state-variable filter
source/minty/InputProfile.h   Synth / guitar tuning constants, one switch
source/minty/DspMath.h        dB helpers, coefficients, soft knee, noise
test/host_render.cpp          Curves, stability, abuse test, renders, speed
```

## Things to verify on real hardware

- **Press vs hold.** The SDK doesn't say whether a hold also fires a press first. I handle both cases: if a hold arrives within 1.2 s of a press, I undo the swell toggle that press caused. If your Endless reports these differently, change `kHoldUndoWindow` in `PatchImpl.cpp`.
- **Input level.** I tuned the synth profile for a line-level source peaking around -1 to -12 dBFS. If your synth comes in much hotter or quieter at the Endless input, move `thresholdAtMinDb` / `thresholdAtMaxDb` in `InputProfile.h`. Ideally the LED goes bright on louder notes and dims between them with SENSITIVITY around noon.
- **CPU.** The host bench runs about 150× realtime on a desktop. My rough guess for the Cortex-M7 is about 1,000 cycles per stereo sample. That's comfortable at the clock speeds this chip family runs at, but I haven't measured it on the device.

Minty is an independent patch inspired by a commercially available pedal. It isn't affiliated with or endorsed by Chase Bliss.
