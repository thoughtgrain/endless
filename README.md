# endless

These are my patches for the Polyend Endless, built on [Polyend's FxPatch SDK](https://github.com/polyend/FxPatchSDK).

| Patch | What it is |
|---|---|
| [`minty/`](minty/) | A creative compressor inspired by the Chase Bliss Clean: feedback compression → limiting → sag on one knob, a dynamic "shifty" EQ, physics wobble, Dusty clipping and dynamic swell. Tuned for synth by default, with a guitar profile as a build flag. |

Each patch folder is a self-contained copy of the SDK, so `cd` into one and run `make` there. You'll need the `arm-none-eabi-` GCC toolchain for the firmware build, plus a desktop `g++` for the host test bench (`make host-test`).

The SDK files in each folder are Polyend's, under their MIT license (see `LICENSE.TXT` in each patch). Minty is an independent project and isn't affiliated with or endorsed by Chase Bliss.
