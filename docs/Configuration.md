# Configuration Reference

The standalone executable reads `spectrum.json` from its own directory. JSON
must be UTF-8 and use standard syntax; comments are not supported. The optional
top-level `_comment` string is ignored. Unknown or obsolete leaf paths reject
the entire file so spelling mistakes cannot silently change runtime behavior.
Object names and JSON value kinds are also strict: numeric settings must be JSON
numbers, switches must be booleans, named modes and colors must be strings, and
`null` is not accepted as a substitute for a value. Unknown groups, including
empty objects, reject the file.

Restart the application after editing the file. Starting a second standalone
instance stops the old process and loads the new configuration. Validation
failures preserve the original file and are written to `spectrum.log`.

## Audio

| Path | Accepted values | Meaning |
|---|---:|---|
| `audio.fftSize` | 4096, 8192, 16384 | FFT length |
| `audio.overlapPercent` | 0–95 | Input reused between adjacent FFT windows |
| `audio.windowFunction` | `hann`, `hamming`, `blackmanHarris` | FFT window |

Bin width is `sampleRate / fftSize`. Hop size is
`fftSize × (1 - overlapPercent / 100)`. A larger FFT improves low-frequency
resolution but increases the analysis window and latency.

## Spectrum

| Path | Accepted values | Meaning |
|---|---:|---|
| `spectrum.bands` | 8–64 | Bar count for Bark, logarithmic, Mel, and linear layouts; third-octave derives it from the center range |
| `spectrum.frequencyScale` | `thirdOctave`, `bark`, `log`, `mel`, `linear` | Band layout |
| `spectrum.minFrequency` | 1–20000 Hz | Lower limit for Bark, logarithmic, Mel, and linear layouts |
| `spectrum.maxFrequency` | 20–24000 Hz | Upper limit, also limited by Nyquist |
| `spectrum.minCenterFrequency` | 1–20000 Hz | Lowest requested nominal third-octave center |
| `spectrum.maxCenterFrequency` | 20–24000 Hz | Highest requested nominal third-octave center |
| `spectrum.referenceFrequency` | 20–20000 Hz | Third-octave reference, normally 1000 Hz |
| `spectrum.bandAggregation` | `peak`, `energy`, `mean`, `slaney` | How FFT-bin power is combined per band |
| `spectrum.frequencyWeighting` | `none`, `A`, `C` | Frequency weighting applied in the FFT domain |
| `spectrum.foldBelowMinimum` | Boolean | Fold non-DC power below the first boundary into the first bar |
| `spectrum.sensitivity` | 25–500 percent | Display gain |
| `spectrum.minimumDecibels` | -160 to -1 dB | Display floor |
| `spectrum.maximumDecibels` | -120 to 0 dB | Full-height level; must exceed the floor |
| `spectrum.attackMs` | 0–2000 ms | Rising display smoothing |
| `spectrum.releaseMs` | 0–5000 ms | Falling display smoothing |

`energy` is recommended for third-octave bands, `peak` for Bark, logarithmic,
and linear bands, and `slaney` only for Mel. These values describe uncalibrated
digital loopback power, not dB SPL, dBA, Class 1, or Class 2 measurements.

## Peak and silence

| Path | Accepted values | Meaning |
|---|---:|---|
| `peak.enabled` | Boolean | Draw peak markers |
| `peak.showWhenSilent` | Boolean | Keep zero-level markers docked at the bottom |
| `peak.holdMs` | 0–5000 ms | Peak hold time |
| `peak.gravity` | 0.1–50 | Visual fall acceleration |
| `peak.height` | 1–8 | Marker height in logical pixels |
| `peak.gap` | 0–8 | Gap above the current bar |
| `silence.enabled` | Boolean | Hide the overlay after sustained silence |
| `silence.threshold` | 0–1 | Threshold in normalized, smoothed bar height |
| `silence.hideDelayMs` | 0–10000 ms | Required silent duration |

## Display, position, and startup

| Path | Accepted values | Meaning |
|---|---:|---|
| `display.framesPerSecond` | 1–100 | UI redraw rate, independent of FFT update rate |
| `display.barColor` | `#RRGGBB` | Low-frequency gradient color |
| `display.secondColor` | `#RRGGBB` | High-frequency gradient color |
| `display.opacity` | 0–255 | Overlay opacity |
| `display.barWidthPercent` | 15–95 | Bar width within each frequency slot |
| `position.automatic` | Boolean | React quickly to taskbar layout changes |
| `position.leftOffset` | 0–4096 | Inset from the search box's left edge |
| `position.rightOffset` | 0–40 | Inset from its right edge |
| `position.topOffset` | 0–12 | Inset from its top edge |
| `position.bottomOffset` | 0–12 | Inset from its bottom edge |
| `startup.startWithWindows` | Boolean | Synchronize the current-user Run registry value at startup |

Offsets are logical pixels and scale with taskbar DPI. The effective rectangle
is the search box rectangle inset by the four configured offsets.

## Preset

[`spectrum.example.json`](../spectrum.example.json) is the maintained default
profile with smoothing and peak markers. It is a visualizer preset, not a
calibrated acoustic measurement profile.
