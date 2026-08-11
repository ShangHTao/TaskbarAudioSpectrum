param(
    [string]$BuildDirectory = 'build\windows-release',
    [switch]$Full,
    [switch]$KeepArtifacts,
    [int]$TakeoverCycles = $(if ($Full) { 50 } else { 10 }),
    [int]$ExplorerCycles = $(if ($Full) { 10 } else { 2 }),
    [int]$SearchModeCycles = $(if ($Full) { 20 } else { 4 }),
    [int]$ZOrderCycles = $(if ($Full) { 30 } else { 5 })
)

$ErrorActionPreference = 'Stop'
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
    ("TaskbarAudioSpectrum-stress-" + [Guid]::NewGuid().ToString('N'))
$testExecutable = Join-Path $testRoot 'TaskbarAudioSpectrum.exe'
$testConfig = Join-Path $testRoot 'spectrum.json'
$testLog = Join-Path $testRoot 'spectrum.log'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$searchKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Search'
$runValueName = 'TaskbarAudioSpectrum'
$eventStart = Get-Date
$originalRunProperty = Get-ItemProperty -LiteralPath $runKey `
    -Name $runValueName -ErrorAction SilentlyContinue
$originalRunExists = $null -ne $originalRunProperty
$originalRunValue = if ($originalRunExists) {
    $originalRunProperty.$runValueName
} else { $null }
$originalSearchMode = (Get-ItemProperty -LiteralPath $searchKey `
    -Name SearchboxTaskbarMode).SearchboxTaskbarMode
$windowsBuild = [int](Get-CimInstance Win32_OperatingSystem).BuildNumber
$fullSearchMode = if ($windowsBuild -ge 22000) { 3 } else { 2 }
$originalProcesses = @(Get-CimInstance Win32_Process `
    -Filter "Name='TaskbarAudioSpectrum.exe'" |
    Select-Object -ExpandProperty ExecutablePath -Unique)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TaskbarNativeMethods {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(
        IntPtr window, out uint processId);
}
'@

function Get-ShellProcessId {
    $taskbar = [TaskbarNativeMethods]::FindWindow('Shell_TrayWnd', $null)
    if ($taskbar -eq [IntPtr]::Zero) { return 0 }
    [uint32]$processId = 0
    [void][TaskbarNativeMethods]::GetWindowThreadProcessId(
        $taskbar, [ref]$processId)
    return $processId
}

function Set-TestConfig([string]$Startup = '') {
    $startupValue = if ($Startup -eq '1') { 'true' } else { 'false' }
    $content = @"
{
  "audio": {
    "fftSize": 8192,
    "overlapPercent": 75,
    "windowFunction": "hann"
  },
  "spectrum": {
    "bands": 24,
    "frequencyScale": "bark",
    "minFrequency": 31.5,
    "maxFrequency": 16000,
    "minCenterFrequency": 31.5,
    "maxCenterFrequency": 16000,
    "referenceFrequency": 1000,
    "bandAggregation": "peak",
    "frequencyWeighting": "none",
    "foldBelowMinimum": true,
    "sensitivity": 180,
    "minimumDecibels": -85,
    "maximumDecibels": -25,
    "attackMs": 35,
    "releaseMs": 220
  },
  "peak": {
    "enabled": true, "showWhenSilent": true,
    "holdMs": 160, "gravity": 3.2,
    "height": 2, "gap": 1
  },
  "silence": {
    "enabled": false, "threshold": 0.015, "hideDelayMs": 500
  },
  "display": {
    "framesPerSecond": 60,
    "barColor": "#EF78D4",
    "secondColor": "#00C2FF",
    "opacity": 180,
    "barWidthPercent": 48
  },
  "position": {
    "automatic": true,
    "leftOffset": 66,
    "rightOffset": 10,
    "topOffset": 5,
    "bottomOffset": 5
  },
  "startup": { "startWithWindows": $startupValue }
}
"@
    [IO.File]::WriteAllText($testConfig, $content,
                            [Text.UTF8Encoding]::new($false))
}

function Wait-Condition([scriptblock]$Condition, [int]$Seconds,
                        [string]$Failure) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    throw $Failure
}

function Stop-TestApp {
    if (Test-Path -LiteralPath $testExecutable) {
        & $testExecutable --exit
        Wait-Condition {
            -not (Get-CimInstance Win32_Process `
                -Filter "Name='TaskbarAudioSpectrum.exe'" |
                Where-Object { $_.ExecutablePath -eq $testExecutable })
        } 10 'Test process did not exit.'
    }
}

function Start-TestApp {
    Start-Process -FilePath $testExecutable -WindowStyle Hidden
    Wait-Condition {
        @(Get-CimInstance Win32_Process `
            -Filter "Name='TaskbarAudioSpectrum.exe'" |
            Where-Object { $_.ExecutablePath -eq $testExecutable }).Count -eq 1
    } 15 'Test process did not start.'
}

function Assert-OverlayVisible {
    Wait-Condition {
        & $probe --require-overlay --require-visible `
            --require-above-taskbar --require-painted *> $null
        $LASTEXITCODE -eq 0
    } 10 'Overlay/shell probe failed.'
    & $probe --require-overlay --require-visible `
        --require-above-taskbar --require-painted
}

try {
    if ($originalSearchMode -ne $fullSearchMode) {
        throw "Stress test requires the full taskbar Search box " +
            "(SearchboxTaskbarMode=$fullSearchMode on build $windowsBuild); " +
            "the current value is $originalSearchMode."
    }
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    Copy-Item -LiteralPath $builtExecutable -Destination $testExecutable
    Set-TestConfig

    foreach ($path in $originalProcesses) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            Start-Process -FilePath $path -ArgumentList '--exit' `
                -WindowStyle Hidden -Wait
        }
    }
    if ($originalProcesses.Count) {
        Wait-Condition {
            -not (Get-CimInstance Win32_Process `
                -Filter "Name='TaskbarAudioSpectrum.exe'" |
                Where-Object { $originalProcesses -contains $_.ExecutablePath })
        } 15 'Original application process did not exit.'
    }
    Start-TestApp
    Wait-Condition {
        if (-not (Test-Path -LiteralPath $testLog -PathType Leaf)) {
            return $false
        }
        try {
            return (Get-Content -LiteralPath $testLog -Raw `
                -ErrorAction Stop) -match 'Search box attached'
        } catch {
            return $false
        }
    } 15 'Overlay did not attach to the search box.'
    $loadedSettings = Get-Content -LiteralPath $testLog -Raw
    $requiredSettings = @(
        'fft=8192 window=hann overlap=75% hop=2048 bars=24 fps=60',
        'sensitivity=1.80 dB=-85.0..-25.0 attack=35ms release=220ms',
        'scale=bark aggregation=peak weighting=none foldLow=1',
        'auto=1 insets=66/10/5/5',
        'showSilent=1 silence=0/0.015/500ms',
        'colors=#EF78D4/#00C2FF'
    )
    foreach ($requiredSetting in $requiredSettings) {
        if (-not $loadedSettings.Contains($requiredSetting)) {
            throw "Runtime configuration was not applied: $requiredSetting"
        }
    }
    Write-Host 'Canonical JSON runtime settings passed.'
    Assert-OverlayVisible

    $zOrderProcess = Get-CimInstance Win32_Process `
        -Filter "Name='TaskbarAudioSpectrum.exe'" |
        Where-Object { $_.ExecutablePath -eq $testExecutable } |
        Select-Object -First 1
    if (-not $zOrderProcess) { throw 'Z-order test process was not found.' }
    for ($cycle = 1; $cycle -le $ZOrderCycles; ++$cycle) {
        & $probe --reclaim-taskbar *> $null
        if ($LASTEXITCODE -ne 0) {
            throw "Taskbar reclaim setup failed in cycle $cycle."
        }
        Wait-Condition {
            & $probe --require-overlay --require-visible `
                --require-above-taskbar --require-painted *> $null
            $LASTEXITCODE -eq 0
        } 2 "Overlay did not recover z-order in cycle $cycle."
        $currentProcess = Get-CimInstance Win32_Process `
            -Filter "Name='TaskbarAudioSpectrum.exe'" |
            Where-Object { $_.ExecutablePath -eq $testExecutable } |
            Select-Object -First 1
        if (-not $currentProcess -or
            $currentProcess.ProcessId -ne $zOrderProcess.ProcessId) {
            throw "Z-order recovery restarted the process in cycle $cycle."
        }
    }
    Write-Host "Taskbar z-order recovery cycles passed: $ZOrderCycles"

    for ($cycle = 1; $cycle -le $TakeoverCycles; ++$cycle) {
        $launched = Start-Process -FilePath $testExecutable `
            -WindowStyle Hidden -PassThru
        Wait-Condition {
            $active = @(Get-CimInstance Win32_Process `
                -Filter "Name='TaskbarAudioSpectrum.exe'" |
                Where-Object { $_.ExecutablePath -eq $testExecutable })
            $active.Count -eq 1 -and $active[0].ProcessId -eq $launched.Id
        } 20 "Takeover cycle $cycle did not settle to one process."
    }
    Write-Host "Instance takeover cycles passed: $TakeoverCycles"
    Assert-OverlayVisible

    $searchModeSkipped = $false
    for ($cycle = 1; $cycle -le $SearchModeCycles; ++$cycle) {
        try {
            Set-ItemProperty -LiteralPath $searchKey `
                -Name SearchboxTaskbarMode -Type DWord -Value 1 `
                -ErrorAction Stop
        } catch [System.UnauthorizedAccessException] {
            Write-Warning ('Search mode cycles skipped because Windows denied ' +
                'access to the taskbar search setting.')
            $searchModeSkipped = $true
            break
        }
        Start-Sleep -Seconds 2
        & $probe --require-overlay --require-hidden
        if ($LASTEXITCODE -ne 0) {
            throw "Search hide cycle $cycle failed."
        }
        Set-ItemProperty -LiteralPath $searchKey -Name SearchboxTaskbarMode `
            -Type DWord -Value $fullSearchMode
        Wait-Condition {
            & $probe --require-overlay --require-visible `
                --require-above-taskbar --require-painted *> $null
            $LASTEXITCODE -eq 0
        } 10 "Search restore cycle $cycle failed."
    }
    if (-not $searchModeSkipped) {
        Write-Host "Search mode cycles passed: $SearchModeCycles"
    }

    for ($cycle = 1; $cycle -le $ExplorerCycles; ++$cycle) {
        $oldExplorerId = Get-ShellProcessId
        if (-not $oldExplorerId) {
            throw "Shell process was unavailable before Explorer cycle $cycle."
        }
        Stop-Process -Id $oldExplorerId -Force
        Wait-Condition {
            $newExplorerId = Get-ShellProcessId
            $newExplorerId -and $newExplorerId -ne $oldExplorerId -and
                $null -ne (Get-Process -Id $newExplorerId `
                    -ErrorAction SilentlyContinue)
        } 30 "Explorer did not restart in cycle $cycle."
        Wait-Condition {
            & $probe --require-overlay --require-visible `
                --require-above-taskbar --require-painted *> $null
            $LASTEXITCODE -eq 0
        } 30 "Overlay did not recover after Explorer cycle $cycle."
        Write-Host "Explorer recovery cycle passed: $cycle/$ExplorerCycles"
    }
    Write-Host "Explorer restart cycles passed: $ExplorerCycles"

    Stop-TestApp
    Set-TestConfig '1'
    Start-TestApp
    Wait-Condition {
        (Get-ItemProperty -LiteralPath $runKey -Name $runValueName `
            -ErrorAction SilentlyContinue).$runValueName -eq
            ('"' + $testExecutable + '"')
    } 5 'Startup registry enable test failed.'
    Stop-TestApp
    Set-TestConfig '0'
    Start-TestApp
    Wait-Condition {
        $null -eq (Get-ItemProperty -LiteralPath $runKey `
            -Name $runValueName -ErrorAction SilentlyContinue)
    } 5 'Startup registry disable test failed.'
    Stop-TestApp
    Write-Host 'Startup registry enable/disable tests passed.'

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
        throw 'New application failure events were detected.'
    }
    if ((Get-Content -LiteralPath $testLog -Raw) -match
        'shutdown incomplete|message loop failed|wait failed') {
        throw 'The stress log contains a runtime shutdown failure.'
    }
    Write-Host 'Stress test passed with no new crash or hang events.'
} finally {
    Set-ItemProperty -LiteralPath $searchKey -Name SearchboxTaskbarMode `
        -Type DWord -Value $originalSearchMode -ErrorAction SilentlyContinue
    if ($originalRunExists) {
        New-ItemProperty -LiteralPath $runKey -Name $runValueName `
            -PropertyType String -Value $originalRunValue -Force |
            Out-Null
    } else {
        Remove-ItemProperty -LiteralPath $runKey -Name $runValueName `
            -ErrorAction SilentlyContinue
    }
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
    if (-not $KeepArtifacts -and $resolvedTestRoot.StartsWith($resolvedTemp,
            [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    } elseif ($KeepArtifacts -and (Test-Path -LiteralPath $resolvedTestRoot)) {
        Write-Host "Stress artifacts retained: $resolvedTestRoot"
    }
}
