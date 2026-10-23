# Granular sample engine

Granular plays the track's sample as a cloud of short, windowed grains taken
from one position, in the spirit of Granulator-style instruments: hold a note to
freeze a moment of the sample, scatter it with spray, or let the position scan
through the file. It uses engine 36 with its own screen page and `singleSample`: the
sample button assigns one sample to the whole track, played chromatically
from the keys.

## Playing it

Granular has its own page (84): a strip stands for the trimmed sample, the
bright marker is the grain position, the dim brackets show the spray range,
the curve below is the grain window at the current size, and for a moment
after each note grains appear along the strip at the engine's rate. Pressing
an encoder switches its position between its two parameters; the header shows
which is active. Shift + turn makes fine adjustments.

| Encoder | Parameter | Press for |
| --- | --- | --- |
| 1 | Position: where in the trimmed sample grains start | Scan: position drift, up to twice real time either way; the middle freezes |
| 2 | Size: grain length, 10 ms to 500 ms | Tune: -12 to +12 semitones |
| 3 | Density: grains per grain length, 0.5 to 8 | Spread: random stereo placement of each grain |
| 4 | Spray: random position offset, up to 2 s | Volume |

- **Pitch.** Grains play at the note's pitch relative to middle C, like the
  stock sampler, plus the pad's tuning and Tune. The grain window is measured
  in output time, so Size does not change with pitch.
- **Trim and reverse.** Playback stays inside the sample's saved start and end
  markers (the whole file for a newly assigned sample); reverse plays every
  grain backwards. Spray is limited to half the trimmed length.
- **Density and level.** Above a density of one grain per grain length, the
  output is scaled down by the overlap so dense clouds keep headroom. The pad
  level and Volume apply afterwards.
- **Presets.** `Cloud` is wide and dense, `Freeze` holds a short slice,
  `Scanner` moves slowly through the file and `Stutter` repeats tight grains.
  New presets use a short attack and a long release.
- The sample's own sample rate is honoured; stereo files keep their channels,
  mono files are placed by Spread.

## How it uses the sample API

[`sample.cpp`](sample.cpp) renders through the
[sample engine API](../../docs/api.md#sample-engines) with `stereoOutput`, and
[`display.cpp`](display.cpp) draws the page through the
[drawing display](../../docs/display.md#drawing-display): the display runs on
the GUI side and simulates grains from the same controls and the note serial;
it reads neither the PCM nor the audio engine's grains.
[`granular.h`](granular.h) holds the voice and grain state and the fixed-point
helpers; [`tables.h`](tables.h) holds the sin² window and semitone ratios.
Each voice keeps up to five grains. The dots in the top-right corner of the page count the display revision. A grain is a position in Q20.12 frames, a
signed step, a window phase and two pan gains; it reads the PCM with linear
interpolation and ends on its window, or when it leaves the trimmed region.
The scheduler starts a grain every `size / density` output samples, placing it
at position plus the scan offset plus a random spray offset, kept inside the
region so no grain is cut short. Playback rate comes from the note's phase
increment times the inverse frequency of middle C, then the tuning ratio and
the source rate; all arithmetic is 32-bit with 16-bit limbs where a product
could exceed it. Sound tests live with the maintainer, outside this repository.
