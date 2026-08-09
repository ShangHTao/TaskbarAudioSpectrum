# Development Guide

## Supported toolchains

The standalone application, tests, and diagnostics use Visual Studio 2022 MSVC
and target Windows 10/11 x86-64. Windhawk uses its bundled Clang/MinGW compiler
for x86 and x64 generated mod targets. C++17 is the shared baseline.

Install the components listed in the repository `.vsconfig`. Local Debug and
Release presets use the Visual Studio generator and work from a normal
PowerShell or VS Code window. The `windows-ci` preset uses Ninja and must run in
a Visual Studio Developer PowerShell with `WINDHAWK_COMPILER_ROOT` set.

## Build and test

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Build the deterministic combined Windhawk source independently when a shared
header, runtime source, metadata, or generator changes:

```powershell
cmake --build build\windows-release --config Release `
  --target tas_windhawk_source
```

For the complete Windhawk build, configure `windows-ci` with the compiler root
and build its preset. Package the standalone release with:

```powershell
cpack --config build\windows-release\CPackConfig.cmake -C Release
```

## Static analysis

The project compiles with `/W4`, UTF-8 source handling, and conforming MSVC
mode. Native Recommended analysis can be run with:

```powershell
cmake --build build\windows-debug --config Debug --target ALL_BUILD -- `
  /p:RunCodeAnalysis=true `
  /p:CodeAnalysisRuleSet=NativeRecommendedRules.ruleset
```

Treat warnings as defects unless a platform contract proves one is a false
positive. Prefer ownership or control-flow changes over warning suppression.

## Source conventions

- Shared contracts live under `include/taskbar_audio_spectrum`; private context
  and implementation helpers stay under `src`.
- Runtime workers receive `ApplicationContext` explicitly. Settings are loaded
  before `Runtime::Start` and remain immutable until shutdown.
- Pure analysis functions accept values and output buffers rather than reading
  cross-thread state.
- Use RAII for Win32 handles where possible and release COM/GDI resources on
  every exit path.
- Keep source, JSON, PowerShell, YAML, and Markdown UTF-8 with LF endings.
- Do not edit or commit generated source, packages, logs, user configuration,
  IDE caches, or anything under `build/`.

## Configuration changes

A new setting requires all of the following:

1. Add its default and type to `Settings`.
2. Add the canonical JSON path and `JsonValueKind` to the standalone schema.
3. Add the equivalent Windhawk metadata and loading key.
4. Update `spectrum.example.json`, the configuration reference, release notes,
   and Wiki documentation when applicable.
5. Add acceptance, wrong-type, boundary, and unknown/obsolete-path tests.

The application never silently accepts a misspelled or incorrectly typed JSON
field.

## Desktop diagnostics

Build with `TAS_BUILD_DIAGNOSTICS=ON` (enabled by local presets). Both scripts
accept either a Visual Studio multi-configuration build directory or a Ninja
single-configuration directory.

```powershell
.\diagnostics\stress_test.ps1
.\diagnostics\stress_test.ps1 -Full
.\diagnostics\soak_test.ps1 -Minutes 15
```

The stress test temporarily changes the taskbar search setting and current-user
startup entry, stops other Taskbar Audio Spectrum instances, and force-restarts
Explorer. It restores captured state in `finally`, but it can disrupt unsaved
shell activity and must run only on an interactive test desktop.

The soak test samples handle count and private memory and checks crash/hang
events. `-LockOnce` intentionally locks the workstation and requires normal
interactive unlock; do not use it in unattended validation.

## Review checklist

- Debug and Release build without warnings.
- All CTest tests pass in both configurations.
- Windhawk combined source is deterministic and both architectures compile.
- CPack contains only intended runtime files.
- `git diff --check` and Markdown links are clean.
- User-visible behavior, limitations, configuration, and release notes are updated.
