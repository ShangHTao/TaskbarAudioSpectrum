# Configuration Reference

The standalone executable reads `spectrum.json` from its own directory at
startup. Start the executable again after editing the file; the new instance
stops the old one and reloads the configuration. If the file is absent, built-in
defaults are used.

Windhawk exposes equivalent controls on its settings page. The Windhawk edition
does not read `spectrum.json`, does not manage Windows startup, and writes to the
Windhawk log instead of `spectrum.log`.

## JSON rules

The standalone configuration must be standard UTF-8 JSON. Its schema is
deliberately strict so a spelling mistake cannot silently change behavior.

- Comments are not supported. An optional top-level `_comment` string is
  accepted and ignored.
- Numeric settings must be JSON numbers, switches must be booleans, and named
  modes and colors must be strings.
- `null`, unknown groups, unknown fields, obsolete paths, and incorrectly typed
  values reject the complete file.
- Missing known fields use their built-in defaults.
- The application never rewrites an invalid file. It shows an error and writes
  the rejected paths to `spectrum.log`.

The ranges below are effective runtime ranges. Out-of-range numeric values are
clamped unless a row states that the default is used. Invalid named values fall
back to their defaults and are logged; invalid colors use their defaults.

## Audio analysis

| Path | Values / effective range | Default | Meaning |
|---|---:|---:|---|
| `audio.fftSize` | `4096`, `8192`, `16384` | `16384` | FFT window length; any other value uses the default |
| `audio.overlapPercent` | `0-95` | `75` | Percentage of input reused by adjacent FFT windows |
| `audio.windowFunction` | `hann`, `hamming`, `blackmanHarris` | `hann` | Window applied before the FFT |

FFT bin width is `device sample rate / fftSize`. The approximate hop size is
`fftSize * (1 - overlapPercent / 100)`. A larger FFT improves low-frequency
resolution but increases the analysis window and latency. More overlap updates
the analysis more often and uses more CPU.

The actual default playback-device sample rate is always used; it is not a
configuration value.

## Frequency bands and levels

| Path | Values / effective range | Default | Meaning |
|---|---:|---:|---|
| `spectrum.frequencyScale` | `thirdOctave`, `bark`, `log`, `mel`, `linear` | `thirdOctave` | Frequency-band layout |
| `spectrum.bands` | `8-64` | `28` | Bar count for Bark, logarithmic, Mel, and linear layouts; ignored by third-octave |
| `spectrum.minFrequency` | `1-20000` Hz | `31.5` | Lower edge for non-third-octave layouts |
| `spectrum.maxFrequency` | `20-24000` Hz | `16000` | Upper edge for non-third-octave layouts, also limited by Nyquist |
| `spectrum.minCenterFrequency` | `1-20000` Hz | `31.5` | Lowest requested nominal third-octave center |
| `spectrum.maxCenterFrequency` | `20-24000` Hz | `16000` | Highest requested nominal third-octave center |
| `spectrum.referenceFrequency` | `20-20000` Hz | `1000` | Reference used to calculate nominal third-octave centers |
| `spectrum.bandAggregation` | `peak`, `energy`, `mean`, `slaney` | `energy` | Method used to combine FFT-bin power in each band |
| `spectrum.frequencyWeighting` | `none`, `A`, `C` | `none` | Weighting applied to FFT-bin power before aggregation |
| `spectrum.foldBelowMinimum` | Boolean | `false` | Add non-DC power below the first boundary to the first visible bar |
| `spectrum.sensitivity` | `25-500` percent | `100` | Display gain applied before dB mapping |
| `spectrum.minimumDecibels` | `-160` to `-1` dB | `-72` | Level mapped to an empty bar |
| `spectrum.maximumDecibels` | `-120` to `0` dB | `-6` | Level mapped to full height; must exceed the minimum |
| `spectrum.attackMs` | `0-2000` ms | `20` | Smoothing time while a bar rises |
| `spectrum.releaseMs` | `0-5000` ms | `220` | Smoothing time while a bar falls |

If the maximum frequency is not above the minimum, the non-third-octave range
returns to `31.5-16000` Hz. Invalid third-octave center ranges, reference
combinations, or band counts return to 28 bands from 31.5 Hz to 16 kHz with a
1 kHz reference. An invalid dB pair returns to `-72` through `-6` dB.

### Layout behavior

- `thirdOctave` creates nominal one-third-octave centers and derives the bar
  count from the center range. `energy` is the recommended aggregation.
- `bark` creates contiguous equal steps on the Bark scale.
- `log` creates contiguous equal-ratio frequency bands.
- `mel` creates overlapping HTK Mel triangles. `slaney` applies area
  normalization and is intended for this layout.
- `linear` creates equal-width bands in hertz.

`peak` is responsive for Bark, logarithmic, and linear layouts. `mean` averages
the weighted power within a band. A/C weighting is applied to relative digital
power and does not turn the result into a calibrated dBA/dBC measurement.

## Peak markers

| Path | Values / effective range | Default | Meaning |
|---|---:|---:|---|
| `peak.enabled` | Boolean | `true` | Draw peak markers |
| `peak.showWhenSilent` | Boolean | `true` | Keep zero-level markers docked at the bottom |
| `peak.holdMs` | `0-5000` ms | `160` | Delay before a marker starts falling |
| `peak.gravity` | `0.1-50` | `3.2` | Visual falling acceleration |
| `peak.height` | `1-8` px | `2` | Marker height at 96 DPI |
| `peak.gap` | `0-8` px | `1` | Gap between the current bar and marker at 96 DPI |

## Silence detection

| Path | Values / effective range | Default | Meaning |
|---|---:|---:|---|
| `silence.enabled` | Boolean | `false` | Hide after sustained silence |
| `silence.threshold` | `0-1` | `0.015` | Normalized smoothed bar height considered active |
| `silence.hideDelayMs` | `0-10000` ms | `500` | Required time below the threshold before hiding |

## Display

| Path | Values / effective range | Default | Meaning |
|---|---:|---:|---|
| `display.framesPerSecond` | `1-100` | `30` | Maximum redraw rate, independent of FFT update rate |
| `display.barColor` | `#RRGGBB` | `#FF78D4` | Low-frequency end of the gradient |
| `display.secondColor` | `#RRGGBB` | `#00C2FF` | High-frequency end of the gradient |
| `display.opacity` | `0-255` | `180` | Alpha used for bars and markers; `0` disables capture and drawing |
| `display.barWidthPercent` | `15-95` | `48` | Bar width within each horizontal frequency slot |

## Position and startup

| Path | Values / effective range | Default | Meaning |
|---|---:|---:|---|
| `position.automatic` | Boolean | `true` | Temporarily use rapid geometry checks after taskbar changes |
| `position.leftOffset` | `0-4096` px | `66` | Inset from the search box left edge |
| `position.rightOffset` | `0-40` px | `10` | Inset from the right edge |
| `position.topOffset` | `0-12` px | `5` | Inset from the top edge |
| `position.bottomOffset` | `0-12` px | `5` | Inset from the bottom edge |
| `startup.startWithWindows` | Boolean | unchanged when absent (`false` in the example) | Synchronize the current-user Run entry at startup; standalone only |

Offsets are logical pixels and scale with the taskbar DPI. Move the standalone
application to its permanent directory before enabling Windows startup because
the registry entry records the current executable path.

## Minimal override example

A file can contain only the known fields you want to override:

```json
{
  "audio": {
    "fftSize": 8192,
    "overlapPercent": 50
  },
  "display": {
    "opacity": 210,
    "barColor": "#7C4DFF",
    "secondColor": "#00E5FF"
  }
}
```

The maintained full profile is
[`spectrum.example.json`](../spectrum.example.json). It is a visualizer preset,
not a calibrated acoustic measurement profile.
