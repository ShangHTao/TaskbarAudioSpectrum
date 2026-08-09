# Troubleshooting

## The spectrum does not appear

1. Set Windows taskbar Search to **Search box**. The icon-only and hidden modes
   do not provide the native rectangle used by the overlay.
2. Play audio through the current default playback device.
3. Close the search flyout; the overlay intentionally hides while search is
   open and while another application is fullscreen.
4. Do not run the standalone executable and Windhawk mod simultaneously.
5. Check `spectrum.log` beside the standalone executable.

The overlay may take a few seconds to reattach after Explorer or the search
process restarts. It should recover without restarting Taskbar Audio Spectrum.

## The application reports an invalid configuration

`spectrum.json` must be UTF-8 standard JSON. It does not support `//` or
`/* ... */` comments. Every field name and value type is validated; unknown,
obsolete, `null`, or incorrectly typed values reject the file.

Start again with [the example](../spectrum.example.json) and compare the logged
path with the [configuration reference](Configuration.md). The application
never rewrites an invalid user file.

## The bars do not match another analyzer

- Confirm the actual Windows mix rate in `spectrum.log`.
- Match FFT length, window, frequency limits, aggregation, weighting, and dB
  range between applications.
- Disable smoothing and peak markers when comparing instantaneous output.
- Remember that loopback values are relative digital power. They are not
  microphone-calibrated dB SPL.

FFT leakage, bin spacing, band-edge weighting, channel-power averaging, and
different time constants can all produce legitimate differences.

## Audio stops updating

Taskbar Audio Spectrum reconnects when the default console playback device
changes. If a driver remains stuck, select another default device and switch
back, or restart the application. A log entry containing a WASAPI HRESULT helps
identify device/driver failures.

## Position or z-order is wrong

- Restore the default position offsets to rule out an invalid visible area.
- Verify Windows display scaling and restart Explorer once after changing
  taskbar tools or shell customizations.
- Temporarily disable other taskbar modification software to identify ownership
  or z-order conflicts.

## Collect useful diagnostics

Include the following with a bug report:

- Windows version and display scaling
- Standalone or Windhawk host and Taskbar Audio Spectrum version
- Sanitized `spectrum.json` and `spectrum.log`
- Playback device format shown in the log
- Whether Explorer, the audio device, or the search mode recently changed

Developers can build `tas_window_probe` and `tas_uia_probe` with
`TAS_BUILD_DIAGNOSTICS=ON`. The stress test changes taskbar settings, restarts
Explorer, and modifies the current-user startup entry temporarily; read
[Development](Development.md) before running it.
