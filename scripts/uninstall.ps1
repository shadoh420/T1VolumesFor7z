<#
.SYNOPSIS
    Removes the T1VolumesFor7z plugin from installed 7-Zip Formats folders.
#>
[CmdletBinding()]
param(
    [string]$SevenZipDir = ""
)

$ErrorActionPreference = "Stop"

$targets = @()
if ($SevenZipDir) {
    $targets += $SevenZipDir
} else {
    foreach ($fallback in @("$env:ProgramFiles\7-Zip", "${env:ProgramFiles(x86)}\7-Zip")) {
        if ($fallback -and (Test-Path $fallback)) {
            $targets += $fallback
        }
    }
}

$removed = 0
foreach ($dir in ($targets | Select-Object -Unique)) {
    $formatsDir = Join-Path $dir "Formats"
    if (Test-Path $formatsDir) {
        $dlls = Get-ChildItem -Path $formatsDir -Filter "TribesVol.*.dll" -ErrorAction SilentlyContinue
        foreach ($d in $dlls) {
            Remove-Item -Path $d.FullName -Force
            Write-Host "Removed $($d.FullName)" -ForegroundColor Yellow
            $removed++
        }
    }
}

if ($removed -eq 0) {
    Write-Host "No TribesVol plugin DLLs were found."
} else {
    Write-Host "Uninstalled successfully." -ForegroundColor Green
}
