# Architecture

Taskbar Audio Spectrum has one reusable runtime and two hosts: the standalone
Windows executable and Windhawk. Both compile the same C++17 implementation.

## Source layout

```text
include/taskbar_audio_spectrum/  Contracts shared across build targets
src/                            Runtime and standalone implementations
src/windhawk/                   Windhawk metadata, adapter, and launcher
tests/                          Deterministic CTest coverage
diagnostics/                    Explorer/UIA probes and desktop endurance tests
cmake/                          Deterministic Windhawk source generation
```

Headers under `include/` are shared project contracts, not a versioned external
SDK. Private ownership types and helpers remain under `src/`.

## Runtime data flow

```text
standalone host ----+
                    +--> Runtime(Settings)
Windhawk host ------+        |
                             +--> audio capture --> spectrum analysis
                             |          |                  |
                             |          +---- band levels -+
                             |
                             +--> search locator --> shared layout
                             |                              |
                             +--> overlay renderer <--------+
```

`Runtime` owns the worker threads and a private `ApplicationContext`. The
context contains the immutable settings snapshot, stop/activity/layout events,
atomic published band levels, visualizer state, shutdown status, and a
synchronized search-layout store. Every start receives a fresh context. If an
external shell call outlives the bounded shutdown period, that stopped context
is isolated for the stale worker and a new runtime can start safely. Worker
entry points receive their context explicitly; there is no global runtime
accessor or transitional `g_*` state.

The audio worker owns ordinary floating-point analysis levels. Spectrum
functions consume explicit settings and plans, update those levels, and do not
know about threads. The worker publishes a completed level set to atomics for
the overlay. This keeps the algorithms deterministic and independently
testable.

The locator uses UI Automation on a cancellable worker because shell calls can
block. Bounding-rectangle and registry notifications drive normal updates;
the Explorer process handle reports shell replacement immediately, and a
30-second UIA refresh remains as a safety net. Its supervisor requests
cancellation during shutdown and republishes the latest valid search-box
layout through the context. A cached geometry template bridges transient UIA
failures.

The overlay is an owned, layered, click-through top-level window. It consumes
the shared layout and band levels, performs elapsed-time smoothing and peak
animation, repairs z-order, and recreates itself when its Explorer owner is
replaced. Registry notifications and foreground/location WinEvents update its
cached visibility state; a 30-second timer is only a failure-recovery fallback.

## Host boundary

`host.h` provides logging and settings/startup operations. The standalone host
reads strict UTF-8 JSON and manages the current-user Run entry. The Windhawk
adapter reads mod settings and never touches standalone files or startup state.

Windhawk source generation concatenates an ordered list of shared headers and
implementations into `build/.../generated/taskbar-audio-spectrum.wh.cpp`. The
generated source is deterministic and never belongs in version control.

## Extension rules

1. Pass new runtime dependencies explicitly; do not add worker globals.
2. Keep settings immutable while workers run. Stop, load, and restart to apply
   a new snapshot.
3. Keep algorithms free of host, window, and synchronization concerns where
   practical.
4. Give every handle, COM interface, GDI object, and allocation a clear owner
   and safe failure path.
5. Treat the JSON structure, leaf names, and value kinds as strict. Document
   and test every schema change.
6. Add deterministic logic to CTest. Reserve registry, Explorer, and endurance
   behavior for `diagnostics/`.

## Build targets

- `tas_core`: common runtime static library
- `TaskbarAudioSpectrum`: standalone GUI executable
- `tas_self_test`: settings, audio, analysis, layout, and rendering logic
- `tas_json_config_test`: JSON parser behavior
- `tas_configuration_schema_test`: schema names, containers, and value kinds
- `tas_window_probe`, `tas_uia_probe`: optional live desktop diagnostics
- `tas_windhawk_source`: deterministic generated Windhawk source
- `tas_windhawk_x86`, `tas_windhawk_x64`: Windhawk DLLs
