# Taskbar Audio Spectrum

[![Build and test](https://github.com/ShangHTao/TaskbarAudioSpectrum/actions/workflows/build.yml/badge.svg)](https://github.com/ShangHTao/TaskbarAudioSpectrum/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/ShangHTao/TaskbarAudioSpectrum)](https://github.com/ShangHTao/TaskbarAudioSpectrum/releases/latest)
[![License](https://img.shields.io/github/license/ShangHTao/TaskbarAudioSpectrum)](LICENSE)

Taskbar Audio Spectrum turns the native Windows 10 or Windows 11 taskbar
search box into a real-time visualizer for system playback. It captures the
default playback mix with WASAPI loopback and draws a lightweight,
click-through overlay without changing how the search box works.

![Taskbar Audio Spectrum running in the Windows taskbar search box](assets/taskbar-spectrum-preview.png)

## Highlights

- Native Windows 10 and Windows 11 search-box integration
- Portable x64 standalone application and Windhawk mod from one shared runtime
- Automatic reconnection when the default playback device changes
- 4,096, 8,192, or 16,384-point FFT with configurable overlap and window
- Third-octave, Bark, logarithmic, Mel, and linear frequency layouts
- Peak, energy, mean, and Slaney band aggregation
- Optional A/C frequency weighting, peak markers, and silence hiding
- Configurable gradient, opacity, smoothing, frame rate, and DPI-scaled offsets
- Recovery after Explorer, Search, display-layout, and taskbar-setting changes
- Event-driven idle mode that pauses rendering after silent bars and peaks settle
- Strict UTF-8 JSON validation for the standalone edition
- Native Windows APIs with no microphone permission or bundled third-party libraries

The displayed levels are relative digital playback power. This project is a
visualizer, not a calibrated sound-level meter, and does not report dB SPL,
dBA, or Class 1/Class 2 measurements.

## Choose an edition

Both editions use the same capture, analysis, search-location, and rendering
code. Choose one and do not run them at the same time.

| Edition | Best for | Configuration and logs |
|---|---|---|
| Standalone | Users who want a small portable program without installing Windhawk | `spectrum.json` and `spectrum.log` beside the executable |
| Windhawk | Existing Windhawk users who want installation, updates, and settings managed by Windhawk | Windhawk settings and Windhawk logs |

The Windhawk edition runs as a dedicated `windhawk.exe` tool process. It does
not inject the audio or rendering runtime into Explorer.

## Requirements

- Windows 10 or Windows 11
- An x64 system for the standalone package
- Taskbar **Search** set to the full **Search box**
- An active default playback device

Search icon-only and hidden modes do not provide the rectangle used by the
overlay. The overlay also hides while the search interface is open or another
application is fullscreen. Audio capture is suspended while the visualizer is
not eligible to appear, reducing unnecessary work.

## Install the standalone application

1. Download `TaskbarAudioSpectrum-v1.1.0-win64.zip` from the
   [latest release](https://github.com/ShangHTao/TaskbarAudioSpectrum/releases/latest).
2. Extract the complete archive to a permanent writable directory.
3. Run `TaskbarAudioSpectrum.exe`.

The package includes the recommended `spectrum.json`. If that file is absent,
the application uses built-in visualizer defaults. An existing Windows startup
entry is changed only when `startup.startWithWindows` is explicitly present.
To apply edits, start the executable again; the new process asks the running
instance to stop and then loads the new configuration.

To stop the standalone application without starting a replacement:

```powershell
TaskbarAudioSpectrum.exe --exit
```

Set `startup.startWithWindows` to `true` in `spectrum.json` to register the
current executable for the current Windows user. Move the application to its
final directory before enabling this option.

## Install with Windhawk

When the mod is available in the Windhawk catalog, open Windhawk, search for
**Taskbar Audio Spectrum**, and select **Install**.

For manual installation, use the generated source from the GitHub release:

1. Download `taskbar-audio-spectrum.wh.cpp` from the
   [latest release](https://github.com/ShangHTao/TaskbarAudioSpectrum/releases/latest).
2. In Windhawk, choose **Create a new mod**.
3. Replace the editor contents with the downloaded source.
4. Compile and enable the mod, then adjust it from the Windhawk settings page.

The `.wh.cpp` file is complete source code, not a prebuilt DLL. GitHub releases
intentionally do not publish Windhawk DLLs.

## Configuration

The maintained default profile uses 28 nominal one-third-octave bands from
31.5 Hz to 16 kHz, a 16,384-point Hann FFT, 75% overlap, energy aggregation,
20 ms attack, 220 ms release, and a 30 FPS display.

For the standalone edition, edit `spectrum.json` beside the executable and
restart it. The parser requires standard UTF-8 JSON:

- comments are not supported, except for the optional top-level `_comment`
  string;
- numbers, booleans, and strings must use their documented JSON types;
- unknown groups, unknown fields, obsolete paths, and `null` values reject the
  file;
- an invalid file is preserved and the reason is written to `spectrum.log`.

Start with [spectrum.example.json](spectrum.example.json) and see the complete
[configuration reference](docs/Configuration.md) for settings, ranges, and
performance tradeoffs. Windhawk users configure the equivalent options in the
Windhawk interface; Windhawk does not read `spectrum.json` or write
`spectrum.log`.

## Release files

| File | Purpose |
|---|---|
| `TaskbarAudioSpectrum-v1.1.0-win64.zip` | Complete portable standalone package |
| `TaskbarAudioSpectrum.exe` | Standalone executable, also included in the ZIP package |
| `taskbar-audio-spectrum.wh.cpp` | Generated source for manual Windhawk installation |
| `SHA256SUMS.txt` | SHA-256 checksums for the executable, ZIP, and generated source |

## Build and test

Install Visual Studio 2022 with **Desktop development with C++** and CMake 3.25
or newer. The included [.vsconfig](.vsconfig) lists the required Visual Studio
components.

```powershell
git clone https://github.com/ShangHTao/TaskbarAudioSpectrum.git
cd TaskbarAudioSpectrum
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Create and test an optimized build, then package it:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cpack --config build/windows-release/CPackConfig.cmake -C Release
```

Visual Studio presets place the executable in the selected configuration
directory, for example
`build/windows-release/Release/TaskbarAudioSpectrum.exe`.

Generate the combined Windhawk source without storing generated code in the
repository:

```powershell
cmake --build build/windows-release --config Release --target tas_windhawk_source
```

See the [development guide](docs/Development.md) for Windhawk compiler setup,
static analysis, diagnostics, and release validation.

## Project structure

```text
include/taskbar_audio_spectrum/  Shared project contracts
src/                            Runtime and standalone implementation
src/windhawk/                   Windhawk metadata, host adapter, and launcher
tests/                          Deterministic automated tests
diagnostics/                    Interactive Windows diagnostics
cmake/                          Deterministic Windhawk source generation
docs/                           User and developer documentation
assets/                         README and Wiki images
```

Private implementation headers belong under `src/`. The `include/` directory
contains contracts shared by build targets, not a separately versioned SDK.
The generated `taskbar-audio-spectrum.wh.cpp` exists only under `build/` and in
release assets.

## Documentation

- [Getting started](https://github.com/ShangHTao/TaskbarAudioSpectrum/wiki/Getting-Started)
- [Configuration reference](docs/Configuration.md)
- [Troubleshooting](docs/Troubleshooting.md)
- [Architecture](docs/Architecture.md)
- [Development guide](docs/Development.md)
- [GitHub Wiki](https://github.com/ShangHTao/TaskbarAudioSpectrum/wiki)

Please use [GitHub Issues](https://github.com/ShangHTao/TaskbarAudioSpectrum/issues)
for reproducible bugs and focused feature requests.

## License

Taskbar Audio Spectrum is available under the [MIT License](LICENSE).
