# Architecture

Taskbar Audio Spectrum has one reusable C++17 runtime and two host adapters:
the portable standalone executable and the Windhawk tool mod. Both hosts use
the same capture, analysis, search-location, and overlay implementation.

## Source layout

```text
include/taskbar_audio_spectrum/  Contracts shared across build targets
src/                            Runtime and standalone implementation
src/windhawk/                   Windhawk metadata, host adapter, and launcher
tests/                          Deterministic CTest coverage
diagnostics/                    Interactive Explorer and UI Automation probes
cmake/                          Deterministic Windhawk source generation
```

Headers under `include/` are project-wide contracts, not a public versioned
SDK. Private ownership types and helpers stay under `src/`.

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

`Runtime` owns three worker threads and a private `ApplicationContext`. The
context contains an immutable settings snapshot, stop/activity/band/layout
events, atomic published band levels, visualizer activity, shutdown status,
and a synchronized search-layout store. Workers receive that context
explicitly; there is no global worker state.

Each start creates a fresh context. Shutdown signals all workers and waits for
bounded completion. If a shell call cannot be cancelled within that period,
the stopped context is deliberately isolated so a later start cannot reuse
state that a stale worker might still access.

## Audio and analysis

The audio worker waits until the overlay is eligible to appear, then captures
the current default console playback endpoint through WASAPI loopback. Endpoint
notifications trigger automatic reconnection. The input format is validated as
PCM integer or IEEE float, including `WAVEFORMATEXTENSIBLE`, before samples are
converted into normalized channel-averaged power.

Spectrum analysis owns ordinary thread-local buffers. It applies the selected
window and FFT, frequency weighting, fractional FFT-bin coverage, band
aggregation, sensitivity, and dB mapping. A complete bar set is published to
atomics only after analysis finishes, keeping the algorithm deterministic and
independently testable.

## Search-box discovery

The locator detects the Windows 10 or Windows 11 Search host and interprets the
corresponding taskbar Search mode. UI Automation finds the native
`SearchButton` rectangle on a cancellable COM worker. Bounding-rectangle and
registry events drive normal updates, while the Explorer process handle detects
shell replacement immediately. A 30-second refresh is retained only as a
recovery path.

The most recent valid taskbar/search geometry is cached as a DPI-aware template
so the overlay can bridge short UI Automation failures during Explorer or
Search restarts. DPI functions are resolved dynamically and fall back to 96 DPI
when an API or window value is unavailable.

## Overlay

The renderer creates an owned, layered, topmost, click-through window from the
current module. It reads the latest layout and published bands, performs
elapsed-time attack/release smoothing and peak animation, and draws a
low-to-high frequency gradient into a 32-bit DIB. Every runtime uses a distinct
window class, so an abandoned thread from a timed-out shutdown cannot block a
fresh renderer from starting.

Registry notifications and foreground/location WinEvents maintain cached
visibility state. Location events are scoped to the foreground process and
coalesced before refreshing geometry. The overlay hides while the search
interface is open, the foreground application is fullscreen, the search box
is unavailable, opacity is zero, or optional silence hiding activates.
Shell/display ineligibility deactivates WASAPI capture; silence hiding keeps
capture active so new audio can make the overlay reappear. Once silent bars and
peaks settle, the frame timer pauses until the audio worker publishes a new
signal. Ownership and z-order are repaired when Explorer changes without
polling them on every frame.

## Host boundary

`host.h` defines the small boundary for logging and settings access.

- The standalone host reads strict UTF-8 JSON, writes `spectrum.log`, controls a
  single process instance, and optionally synchronizes the current-user Run
  registry value.
- The Windhawk host reads mod settings, writes directly with `Wh_Log`, and owns
  a process-lifetime optional `Runtime` that is explicitly destroyed from the
  normal unload callback.

The Windhawk launcher runs the mod in a dedicated `windhawk.exe` tool process,
so runtime code is not injected into Explorer.

## Generated Windhawk source

`cmake/GenerateWindhawk.cmake` concatenates an ordered list of shared headers
and implementations into
`build/.../generated/taskbar-audio-spectrum.wh.cpp`. Host-only sections are
excluded and shared standalone logging calls are transformed into direct
Windhawk logging calls. The output is deterministic, complete, and never
tracked in the main repository.

## Extension rules

1. Pass new runtime dependencies through `ApplicationContext`; do not add
   worker globals.
2. Keep settings immutable while workers run. Stop, load, and restart to apply
   a new snapshot.
3. Keep analysis functions independent of host, window, and synchronization
   concerns where practical.
4. Give every handle, COM interface, GDI object, callback, and allocation a
   clear owner and failure path.
5. Treat JSON paths, containers, and value kinds as a strict public schema.
6. Document and test every user-visible setting change in both hosts.
7. Put deterministic logic in CTest and reserve live registry, Explorer, UIA,
   and endurance behavior for `diagnostics/`.

## Build targets

- `tas_core`: shared runtime static library
- `TaskbarAudioSpectrum`: standalone GUI executable
- `tas_self_test`: settings, analysis, layout, and rendering logic
- `tas_json_config_test`: JSON parser behavior
- `tas_configuration_schema_test`: schema names, groups, and value kinds
- `tas_window_probe`, `tas_uia_probe`: optional live desktop diagnostics
- `tas_windhawk_source`: deterministic combined Windhawk source
- `tas_windhawk_x86`, `tas_windhawk_x64`: local Windhawk validation DLLs
