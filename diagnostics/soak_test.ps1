param(
    [string]$BuildDirectory = 'build\windows-release',
    [int]$Minutes = 15,
    [int]$SampleSeconds = 15,
    [int]$WarmupSeconds = 10,
    [switch]$LockOnce
)

$ErrorActionPreference = 'Stop'
if ($Minutes -lt 1 -or $SampleSeconds -lt 5 -or $WarmupSeconds -lt 0) {
    throw 'Minutes must be at least 1, SampleSeconds at least 5, and WarmupSeconds non-negative.'
}
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $repository $BuildDirectory
function Resolve-BuildOutput([string]$Name) {
    $candidates = @(
        (Join-Path $buildRoot $Name),
        (Join-Path (Join-Path $buildRoot 'Release') $Name)
    )
    return $candidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
}
$builtExecutable = Resolve-BuildOutput 'TaskbarAudioSpectrum.exe'
$probe = Resolve-BuildOutput 'tas_window_probe.exe'
if (-not $builtExecutable -or -not $probe) {
    throw 'Release outputs are missing. Build the windows-release preset first.'
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("TaskbarAudioSpectrum-soak-" + $PID)
$testExecutable = Join-Path $testRoot 'TaskbarAudioSpectrum.exe'
$testConfig = Join-Path $testRoot 'spectrum.json'
$testLog = Join-Path $testRoot 'spectrum.log'
$eventStart = Get-Date
$originalProcesses = @(Get-CimInstance Win32_Process `
    -Filter "Name='TaskbarAudioSpectrum.exe'" |
    Select-Object -ExpandProperty ExecutablePath -Unique)

function Wait-Condition([scriptblock]$Condition, [int]$Seconds,
                        [string]$Failure) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw $Failure
}

function Stop-TestApp {
    if (-not (Test-Path -LiteralPath $testExecutable)) { return }
    & $testExecutable --exit
    Wait-Condition {
        -not (Get-CimInstance Win32_Process `
            -Filter "Name='TaskbarAudioSpectrum.exe'" |
            Where-Object { $_.ExecutablePath -eq $testExecutable })
    } 10 'Soak process did not exit.'
}

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    Copy-Item -LiteralPath $builtExecutable -Destination $testExecutable
    $builtConfig = Join-Path (Split-Path $builtExecutable) 'spectrum.json'
    $sourceConfig = if (Test-Path -LiteralPath $builtConfig) {
        $builtConfig
    } else {
        Join-Path $repository 'spectrum.example.json'
    }
    Copy-Item -LiteralPath $sourceConfig -Destination $testConfig
    foreach ($path in $originalProcesses) {
        if ($path -and (Test-Path -LiteralPath $path)) { & $path --exit }
    }
    Start-Process -FilePath $testExecutable -WindowStyle Hidden
    Wait-Condition {
        & $probe --require-overlay --require-visible `
            --require-above-taskbar --require-painted *> $null
        $LASTEXITCODE -eq 0
    } 20 'Overlay did not become visible for the soak test.'

    $processInfo = Get-CimInstance Win32_Process `
        -Filter "Name='TaskbarAudioSpectrum.exe'" |
        Where-Object { $_.ExecutablePath -eq $testExecutable } |
        Select-Object -First 1
    if (-not $processInfo) { throw 'Soak process was not found.' }
    $soakProcessId = $processInfo.ProcessId
    if ($WarmupSeconds) {
        Write-Host "Waiting $WarmupSeconds seconds for runtime initialization."
        Start-Sleep -Seconds $WarmupSeconds
        & $probe --require-overlay --require-visible `
            --require-above-taskbar --require-painted *> $null
        if ($LASTEXITCODE -ne 0) {
            throw 'Overlay probe failed after soak-test warmup.'
        }
    }
    $initial = Get-Process -Id $soakProcessId
    $initialHandles = $initial.HandleCount
    $initialPrivate = $initial.PrivateMemorySize64
    $maximumHandles = $initialHandles
    $maximumPrivate = $initialPrivate

    if ($LockOnce) {
        Add-Type @'
using System.Runtime.InteropServices;
public static class SessionLockNativeMethods {
    [DllImport("user32.dll")]
    public static extern bool LockWorkStation();
}
'@
        Write-Host 'Locking the workstation once; unlock it normally to continue.'
        if (-not [SessionLockNativeMethods]::LockWorkStation()) {
            throw 'LockWorkStation failed.'
        }
    }

    $sampleCount = [Math]::Ceiling($Minutes * 60 / $SampleSeconds)
    for ($sample = 1; $sample -le $sampleCount; ++$sample) {
        Start-Sleep -Seconds $SampleSeconds
        $process = Get-Process -Id $soakProcessId -ErrorAction Stop
        $maximumHandles = [Math]::Max($maximumHandles, $process.HandleCount)
        $maximumPrivate = [Math]::Max(
            $maximumPrivate, $process.PrivateMemorySize64)
        & $probe --require-overlay --require-visible `
            --require-above-taskbar --require-painted *> $null
        if ($LASTEXITCODE -ne 0) {
            & $probe --require-overlay --require-hidden *> $null
            if ($LASTEXITCODE -ne 0) {
                throw "Overlay/shell probe failed at soak sample $sample."
            }
        }
        if ($sample % [Math]::Max(1, [Math]::Floor(60 / $SampleSeconds)) `
            -eq 0) {
            Write-Host ("Soak progress: {0}/{1} min, handles={2}, private={3:N1} MiB" -f `
                [Math]::Round($sample * $SampleSeconds / 60, 1), $Minutes,
                $process.HandleCount,
                ($process.PrivateMemorySize64 / 1MB))
        }
    }

    $final = Get-Process -Id $soakProcessId
    if ($final.HandleCount -gt $initialHandles + 12) {
        throw "Handle growth exceeded limit: $initialHandles -> $($final.HandleCount)."
    }
    if ($final.PrivateMemorySize64 -gt $initialPrivate + 32MB) {
        throw 'Private-memory growth exceeded 32 MiB.'
    }
    Stop-TestApp

    $failureEvents = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'
        StartTime = $eventStart
        Id = 1000,1001,1002
    } -ErrorAction SilentlyContinue | Where-Object {
        $_.Message -match 'TaskbarAudioSpectrum' -or
        ($_.Message -match 'explorer.exe' -and
         $_.Message -match 'AppHangXProc')
    })
    if ($failureEvents.Count) {
        $failureEvents | Format-List TimeCreated,Id,ProviderName,Message
        throw 'New application failure events were detected during soak.'
    }
    if ((Get-Content -LiteralPath $testLog -Raw) -match
        'shutdown incomplete|did not stop|message loop failed|wait failed') {
        throw 'The soak log contains a runtime shutdown failure.'
    }
    $summary = ("Soak test passed: initial/final/max handles={0}/{1}/{2}, " +
        "initial/final/max private={3:N1}/{4:N1}/{5:N1} MiB") -f `
        $initialHandles, $final.HandleCount, $maximumHandles,
        ($initialPrivate / 1MB), ($final.PrivateMemorySize64 / 1MB),
        ($maximumPrivate / 1MB)
    Write-Host $summary
} finally {
    try { Stop-TestApp } catch { Write-Warning $_ }
    foreach ($path in $originalProcesses) {
        if ($path -and (Test-Path -LiteralPath $path) -and
            -not (Get-CimInstance Win32_Process `
                -Filter "Name='TaskbarAudioSpectrum.exe'" |
                Where-Object { $_.ExecutablePath -eq $path })) {
            Start-Process -FilePath $path -WindowStyle Hidden
        }
    }
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTestRoot.StartsWith($resolvedTemp,
            [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
