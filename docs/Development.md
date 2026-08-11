# Development Guide

## Supported toolchains

The standalone application, automated tests, and diagnostics use Visual Studio
2022 MSVC and target Windows 10/11 x64. C++17 is the shared language baseline.
Install the components listed in the repository [.vsconfig](../.vsconfig).

The generated Windhawk mod uses Windhawk's bundled Clang/MinGW toolchain. The
project CMake targets validate x86 and x64 mod builds. Generated `.wh.cpp`
source is an output, never an authored source file.

Local Debug and Release presets use the Visual Studio generator and work from a
normal PowerShell or VS Code terminal. The `windows-ci` preset uses Ninja and
must run in a Visual Studio Developer PowerShell with
`WINDHAWK_COMPILER_ROOT` pointing to the Windhawk compiler directory that
contains `bin/clang++.exe`.

## Build and test

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Visual Studio builds place the executable under the configuration directory,
for example `build/windows-release/Release/TaskbarAudioSpectrum.exe`.

Package the standalone release with:

```powershell
cpack --config build/windows-release/CPackConfig.cmake -C Release
```

## Windhawk source and compiler validation

Regenerate the combined source whenever a shared header, runtime source,
metadata block, host adapter, or generator changes:

```powershell
cmake --build build/windows-release --config Release `
  --target tas_windhawk_source
```

For x86/x64 compiler validation, open a Visual Studio Developer PowerShell and
configure the CI preset:

```powershell
$env:WINDHAWK_COMPILER_ROOT = 'C:\path\to\Windhawk\Compiler'
cmake --preset windows-ci
cmake --build --preset windows-ci
ctest --preset windows-ci
```

The generated file is
`build/windows-ci/generated/taskbar-audio-spectrum.wh.cpp`. Verify that a
second generation produces the same hash. Do not edit the generated file to
fix a review issue; update its source fragments or generator instead.

## Static analysis

The project compiles with `/W4`, UTF-8 source handling, and conforming MSVC
mode. Run Native Recommended analysis with:

```powershell
cmake --build build/windows-debug --config Debug --target ALL_BUILD -- `
  /p:RunCodeAnalysis=true `
  /p:CodeAnalysisRuleSet=NativeRecommendedRules.ruleset
```

Treat warnings as defects unless a platform contract proves one is a false
positive. Prefer ownership or control-flow changes over warning suppression.

## Source conventions

- Shared contracts belong under `include/taskbar_audio_spectrum`; private
  context and implementation helpers belong under `src`.
- Runtime workers receive `ApplicationContext` explicitly. Settings are loaded
  before `Runtime::Start` and remain immutable until shutdown.
- Pure analysis functions accept values and output buffers rather than reading
  cross-thread state.
- Use RAII for Win32 handles where possible and release COM/GDI resources on
  every exit path.
- Keep source, JSON, PowerShell, YAML, and Markdown as UTF-8 with LF endings.
- Do not commit generated source, packages, logs, user configuration, IDE
  caches, or anything under `build/`.

## Configuration changes

A new setting requires all of the following:

1. Add its default and type to `Settings`.
2. Add the canonical standalone JSON path and `JsonValueKind`.
3. Add the equivalent Windhawk metadata and loading key when applicable.
4. Update `spectrum.example.json`, the configuration reference, README, Wiki,
   and release notes.
5. Add acceptance, wrong-type, boundary, and unknown/obsolete-path tests.

The standalone application must never silently accept a misspelled or
incorrectly typed JSON field.

## Desktop diagnostics

Local presets enable `TAS_BUILD_DIAGNOSTICS`. Both scripts accept Visual Studio
multi-configuration and Ninja single-configuration build directories.

```powershell
.\diagnostics\stress_test.ps1
.\diagnostics\stress_test.ps1 -Full
.\diagnostics\soak_test.ps1 -Minutes 15
```

The stress test temporarily changes taskbar Search and the current-user startup
entry, stops other Taskbar Audio Spectrum instances, and force-restarts
Explorer. It restores captured state in `finally`, but it can disrupt unsaved
shell activity and must run only on an interactive test desktop. Set taskbar
Search to the full **Search box** before starting it; the script verifies the
OS-specific Windows 10/11 mode before changing system state.

The soak test samples handle count and private memory and checks crash/hang
events. `-LockOnce` intentionally locks the workstation and requires a normal
interactive unlock; do not use it in unattended validation.

## Review checklist

- Debug and Release build without warnings.
- All CTest tests pass in Debug, Release, and CI configurations.
- Native Recommended static analysis passes.
- Windhawk source generation is deterministic.
- Generated Windhawk source passes repository validation and compiler checks.
- CPack and GitHub Release contain only intended artifacts.
- `git diff --check`, line endings, and Markdown links are clean.
- README, Wiki, configuration reference, and release notes match behavior.
