// ==WindhawkMod==
// @id              taskbar-audio-spectrum
// @name            Taskbar Audio Spectrum
// @description     Render the system audio spectrum in the native Windows 10/11 taskbar search box
// @version         __TAS_VERSION__
// @author          shanght
// @github          https://github.com/ShangHTao
// @homepage        https://github.com/ShangHTao/TaskbarAudioSpectrum
// @license         MIT
// @include         windhawk.exe
// @architecture    x86
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -luuid -lgdi32 -ladvapi32 -lshell32 -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Audio Spectrum

![Taskbar Audio Spectrum preview](https://raw.githubusercontent.com/ShangHTao/TaskbarAudioSpectrum/main/assets/taskbar-spectrum-preview.png)

Displays a click-through real-time spectrum over the native Windows 10 or
Windows 11 taskbar search box. System playback is captured with WASAPI
loopback, so microphone permission isn't required and the search box remains
clickable.

## Requirements

Set the Windows taskbar Search option to the full **Search box** before enabling
the mod. Do not run the standalone Taskbar Audio Spectrum application at the
same time because both versions render the same overlay.

The `thirdOctave` scale derives its number of bars from the minimum and maximum
center frequencies. `nonOctaveBarCount` applies only to Bark, logarithmic, Mel,
and linear scales. Rapid taskbar layout tracking temporarily increases geometry
checks after the taskbar changes, which helps the overlay follow animations and
layout updates.

The default 16384-point FFT prioritizes low-frequency resolution. Select 4096
for faster-looking motion when resolving the lowest third-octave bands is less
important.

Unlike **Taskbar Fluent Media Player**, this mod doesn't add a media-player
widget. Unlike **Desktop Audio Visualizer**, it renders inside the taskbar search
box. It runs in a dedicated `windhawk.exe` tool process instead of injecting the
audio and rendering runtime into Explorer.

Source, standalone downloads, and detailed documentation are available on the
[project page](https://github.com/ShangHTao/TaskbarAudioSpectrum).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- frequencyScale: "thirdOctave"
  $name: Frequency scale
  $description: Third-octave uses the center-frequency range; other scales use the non-octave bar count.
  $options:
  - "thirdOctave": Third-octave
  - "bark": Bark
  - "log": Logarithmic
  - "mel": Mel
  - "linear": Linear
- nonOctaveBarCount: 28
  $name: Non-octave bar count
  $description: Number of bars for Bark, logarithmic, Mel, and linear scales; ignored by third-octave.
- minimumFrequency: "31.5"
  $name: Minimum frequency (Hz)
  $description: Lower analysis edge for non-octave scales; ignored by third-octave.
- maximumFrequency: "16000"
  $name: Maximum frequency (Hz)
  $description: Upper analysis edge for non-octave scales; ignored by third-octave.
- minimumCenterFrequency: "31.5"
  $name: Minimum third-octave center (Hz)
  $description: Lowest nominal center for third-octave; ignored by other scales.
- maximumCenterFrequency: "16000"
  $name: Maximum third-octave center (Hz)
  $description: Highest nominal center for third-octave; ignored by other scales.
- referenceFrequency: "1000"
  $name: Third-octave reference frequency (Hz)
  $description: Reference used to calculate nominal third-octave centers; ignored by other scales.
- bandAggregation: "energy"
  $name: Band amplitude calculation
  $description: Method used to combine FFT bins. Slaney normalization is specific to Mel; on other scales it matches energy.
  $options:
  - "peak": Peak
  - "energy": Energy
  - "mean": Mean
  - "slaney": Slaney-normalized
- frequencyWeighting: "none"
  $name: Frequency weighting
  $description: Optional perceptual weighting applied before band aggregation.
  $options:
  - "none": None
  - "A": A-weighting
  - "C": C-weighting
- foldBelowMinimum: false
  $name: Fold lower frequencies into first bar
  $description: Adds energy below the configured minimum to the first visible band on third-octave, Bark, logarithmic, and linear scales; ignored by Mel.
- fftSize: 16384
  $name: FFT size
  $description: Analysis window size. 4096 reacts faster; 16384 resolves the lowest third-octave bands more accurately but adds latency. Supported values are 4096, 8192, and 16384.
- overlapPercent: 75
  $name: FFT overlap (percent)
  $description: Overlap between consecutive analysis windows. Higher values update the analysis more often and use more CPU.
- windowFunction: "hann"
  $name: FFT window
  $description: Window function applied before each FFT.
  $options:
  - "hann": Hann
  - "hamming": Hamming
  - "blackmanHarris": Blackman-Harris
- framesPerSecond: 30
  $name: Frames per second
  $description: Maximum visual refresh rate.
- barColor: "#FF78D4"
  $name: Gradient start color
  $description: RGB color for the first spectrum bar.
- secondColor: "#00C2FF"
  $name: Gradient end color
  $description: RGB color for the last spectrum bar.
- opacity: 180
  $name: Opacity (0-255)
  $description: Alpha value used to draw bars and peaks.
- sensitivity: 100
  $name: Sensitivity (percent)
  $description: Gain applied to the analyzed spectrum before display mapping.
- minimumDecibels: "-72"
  $name: Minimum display level (dB)
  $description: Signal level mapped to an empty bar.
- maximumDecibels: "-6"
  $name: Maximum display level (dB)
  $description: Signal level mapped to a full-height bar.
- attackMs: 20
  $name: Bar attack time (ms)
  $description: Smoothing time while a bar rises.
- releaseMs: 220
  $name: Bar release time (ms)
  $description: Smoothing time while a bar falls.
- peakEnabled: true
  $name: Show peak blocks
  $description: Draws a falling peak marker above each bar.
- peakShowWhenSilent: true
  $name: Keep peak blocks visible during silence
  $description: Leaves peak markers at the baseline when their levels reach zero.
- peakHoldMs: 160
  $name: Peak hold time (ms)
  $description: Delay before a peak marker starts falling.
- peakGravity: "3.2"
  $name: Peak fall gravity
  $description: Acceleration applied to falling peak markers.
- peakHeight: 2
  $name: Peak block height (pixels)
  $description: Peak marker height at 96 DPI.
- peakGap: 1
  $name: Peak block gap (pixels)
  $description: Gap between each bar and its peak marker at 96 DPI.
- hideWhenSilent: false
  $name: Hide when silent
  $description: Hides the overlay after the signal remains below the silence threshold.
- silenceThreshold: "0.015"
  $name: Silence threshold
  $description: Normalized display level considered audible.
- silenceHideDelayMs: 500
  $name: Silence hide delay (ms)
  $description: Time below the threshold before the overlay is hidden.
- bottomPadding: 5
  $name: Bottom edge offset (pixels)
  $description: Empty space below the spectrum at 96 DPI.
- topPadding: 5
  $name: Top edge offset (pixels)
  $description: Empty space above the spectrum at 96 DPI.
- spectrumLeftOffset: 66
  $name: Left edge offset (pixels)
  $description: Space reserved for the search icon at 96 DPI.
- rightPadding: 10
  $name: Right edge offset (pixels)
  $description: Empty space at the right edge at 96 DPI.
- barWidthPercent: 48
  $name: Bar width (percent of slot)
  $description: Width of each bar relative to its allocated horizontal slot.
- autoPosition: true
  $name: Rapid taskbar layout tracking
  $description: Temporarily checks geometry more frequently after taskbar layout changes.
*/
// ==/WindhawkModSettings==
