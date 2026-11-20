<#
.SYNOPSIS
    Installs the T1VolumesFor7z plugin into your 7-Zip Formats folder.
.DESCRIPTION
    Detects installed 64-bit and/or 32-bit 7-Zip installations, creates the
    Formats subfolder if needed, and installs only the architecture-matching DLL.
#>
[CmdletBinding()]
param(
    [string]$SevenZipDir = "",
    [string]$DllDir = ""
)

$ErrorActionPreference = "Stop"

# Auto-locate DLL directory (either next to this script or in repo build output).
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $DllDir) {
    $candidates = @(
        $ScriptRoot,
        (Split-Path -Parent $ScriptRoot),
        (Join-Path (Split-Path -Parent $ScriptRoot) "build-x64\Release")
    )
    foreach ($c in $candidates) {
        if ((Test-Path (Join-Path $c "TribesVol.64.dll")) -or (Test-Path (Join-Path $c "TribesVol.32.dll"))) {
            $DllDir = $c
            break
        }
    }
}

function Find-Dll([string]$Name) {
    $paths = @(
        (Join-Path $DllDir $Name),
        (Join-Path (Split-Path -Parent $ScriptRoot) "build-x64\Release\$Name"),
        (Join-Path (Split-Path -Parent $ScriptRoot) "build-x86\Release\$Name")
    )
    foreach ($p in $paths) {
        if ($p -and (Test-Path $p)) { return (Resolve-Path $p).Path }
    }
    return $null
}

function Get-PeMachine([string]$ExePath) {
    $bytes = [System.IO.File]::ReadAllBytes($ExePath)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    switch ($machine) {
        0x8664 { return "x64" }
        0x014C { return "x86" }
        0xAA64 { return "arm64" }
        default { return "unknown" }
    }
}

$targets = @()
if ($SevenZipDir) {
    $targets += $SevenZipDir
} else {
    $regPaths = @(
        @{ Key = "HKLM:\SOFTWARE\7-Zip"; Val = "Path64" },
        @{ Key = "HKLM:\SOFTWARE\7-Zip"; Val = "Path" },
        @{ Key = "HKLM:\SOFTWARE\WOW6432Node\7-Zip"; Val = "Path32" },
        @{ Key = "HKLM:\SOFTWARE\WOW6432Node\7-Zip"; Val = "Path" },
        @{ Key = "HKCU:\SOFTWARE\7-Zip"; Val = "Path64" },
        @{ Key = "HKCU:\SOFTWARE\7-Zip"; Val = "Path" }
    )
    foreach ($entry in $regPaths) {
        if (Test-Path $entry.Key) {
            $val = (Get-ItemProperty -Path $entry.Key -Name $entry.Val -ErrorAction SilentlyContinue).($entry.Val)
            if ($val -and (Test-Path (Join-Path $val "7z.dll"))) {
                $targets += $val.TrimEnd('\')
            }
        }
    }
    foreach ($fallback in @("$env:ProgramFiles\7-Zip", "${env:ProgramFiles(x86)}\7-Zip")) {
        if ($fallback -and (Test-Path (Join-Path $fallback "7z.dll"))) {
            $targets += $fallback
        }
    }
}

$targets = $targets | Select-Object -Unique
if ($targets.Count -eq 0) {
    Write-Error "Could not find an installed 7-Zip directory. Pass -SevenZipDir <path> manually."
    exit 1
}

$installed = 0
foreach ($dir in $targets) {
    $dll7z = Join-Path $dir "7z.dll"
    if (-not (Test-Path $dll7z)) {
        Write-Warning "Skipping $dir (7z.dll not found)"
        continue
    }
    $arch = Get-PeMachine $dll7z
    $dllName = switch ($arch) {
        "x64" { "TribesVol.64.dll" }
        "x86" { "TribesVol.32.dll" }
        default { $null }
    }
    if (-not $dllName) {
        Write-Warning "Unsupported 7-Zip architecture ($arch) at $dir"
        continue
    }
    $srcDll = Find-Dll $dllName
    if (-not $srcDll) {
        Write-Warning "Could not locate $dllName for $arch 7-Zip at $dir"
        continue
    }

    $formatsDir = Join-Path $dir "Formats"
    try {
        New-Item -ItemType Directory -Force -Path $formatsDir | Out-Null
        # Remove any mismatched-architecture TribesVol DLL so 7-Zip doesn't log a load error.
        Get-ChildItem -Path $formatsDir -Filter "TribesVol.*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
        Copy-Item -Path $srcDll -Destination (Join-Path $formatsDir $dllName) -Force
        Write-Host "Installed $dllName -> $formatsDir" -ForegroundColor Green
        $installed++
    } catch [System.UnauthorizedAccessException] {
        Write-Error "Access denied writing to $formatsDir. Please re-run PowerShell as Administrator."
        exit 1
    }
}

if ($installed -eq 0) {
    Write-Error "No plugin DLLs were installed."
    exit 1
}

Write-Host "Done! Restart 7-Zip File Manager if it is currently open." -ForegroundColor Cyan
