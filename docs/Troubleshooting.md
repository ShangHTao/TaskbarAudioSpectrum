# Troubleshooting

## The spectrum does not appear

1. Set Windows taskbar **Search** to the full **Search box**. Search icon-only
   and hidden modes do not expose the rectangle used by the overlay.
2. Play audio through the current default playback device.
3. Close the search interface and leave fullscreen mode. The overlay
   intentionally hides in both cases.
4. Verify that `display.opacity` is greater than `0` and that the position
   offsets leave a visible rectangle.
5. Run only one edition: standalone or Windhawk.

For the standalone edition, check `spectrum.log` beside the executable. For the
Windhawk edition, open the mod's Windhawk log; Windhawk does not create
`spectrum.log`.

Explorer and Search restarts can take a few seconds to settle. The cached
geometry should bridge short interruptions and the overlay should reattach
without restarting Taskbar Audio Spectrum.

## The application reports an invalid configuration

`spectrum.json` must be standard UTF-8 JSON. It does not support `//` or
`/* ... */` comments. Field names, groups, and value types are strict. Unknown,
obsolete, `null`, or incorrectly typed values reject the complete file.

Start again with
[`spectrum.example.json`](../spectrum.example.json) and compare the rejected
path in `spectrum.log` with the [configuration reference](Configuration.md).
The application never rewrites an invalid user file.

If `spectrum.json` is missing, that is not an error: built-in defaults are
used. A file that exists but cannot be read is treated as an error.

## Configuration changes do not apply

The standalone application loads settings only at startup. Start
`TaskbarAudioSpectrum.exe` again to replace the running instance and reload the
file. If the previous instance cannot stop within 15 seconds, end its
`TaskbarAudioSpectrum` process and try again.

Windhawk settings trigger a runtime restart automatically. Review the Windhawk
log if the restart fails.

## The bars do not match another analyzer

- Confirm the actual Windows mix rate in the log.
- Match FFT length, overlap, window, frequency limits, aggregation, weighting,
  sensitivity, and dB range.
- Disable smoothing and peak markers when comparing instantaneous output.
- Remember that loopback levels are relative digital power, not microphone-
  calibrated dB SPL.

FFT leakage, bin spacing, fractional band edges, channel-power averaging, and
different time constants can all produce legitimate differences.

## Audio stops updating

The runtime reconnects when the default console playback device changes. If a
driver remains stuck, select another default playback device and switch back,
or restart the host. Include any logged WASAPI HRESULT in a bug report.

Capture intentionally stops while the search box is unavailable, the search
interface is open, a fullscreen application is in front, or opacity is zero. It
resumes when the overlay becomes eligible again. Silence hiding is different:
analysis continues so new audio can make the overlay reappear.

## Position or z-order is wrong

- Restore the default offsets: left `66`, right `10`, top `5`, bottom `5`.
- Verify Windows display scaling and reconnect or restart Explorer after major
  taskbar/display changes.
- Temporarily disable other taskbar modification software to identify owner or
  z-order conflicts.
- Keep `position.automatic` enabled if the taskbar layout changes frequently.

Offsets use logical pixels and are scaled for the taskbar DPI.

## Windows startup points to the wrong file

The standalone edition records its current full executable path when
`startup.startWithWindows` is enabled. Set it to `false`, run the application
once, move the complete folder, set it to `true`, and run it again. The setting
is per user and is not used by Windhawk.

## Collect useful diagnostics

Include the following in a
[bug report](https://github.com/ShangHTao/TaskbarAudioSpectrum/issues):

- Windows version, taskbar Search mode, and display scaling
- Standalone or Windhawk edition and Taskbar Audio Spectrum version
- Sanitized `spectrum.json` and `spectrum.log`, or the relevant Windhawk log
- Playback device format shown in the log
- Whether Explorer, Search, the audio device, or the display layout changed
- Other installed taskbar customization software

Developers can build `tas_window_probe` and `tas_uia_probe`. The stress test
changes taskbar settings, restarts Explorer, and temporarily modifies the
current-user startup entry; read [Development](Development.md) before running
it.
