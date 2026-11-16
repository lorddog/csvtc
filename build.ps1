# CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
#
# Copyright (c) 2026 LordDog
# Open source — feel free to use, modify, and distribute.
# Credits are appreciated but not required.
#
# Aufruf:  .\build.ps1 [-Config Release|Debug] [-Arch all|x64|x86] [-Zip] [-NoTest]

param(
    [string]$Config = "Release",
    [ValidateSet("all", "x64", "x86")][string]$Arch = "all",
    [switch]$Zip,
    [switch]$NoTest
)

$root = $PSScriptRoot
$dist = "$root\dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

function Build([string]$platform, [string]$dir) {
    Write-Host "=== $platform ===" -ForegroundColor Cyan
    cmake -B "$root\$dir" -A $platform "$root"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build "$root\$dir" --config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if (-not $NoTest) {
        & "$root\$dir\bin\csvtest.exe"
        if ($LASTEXITCODE -ne 0) { Write-Host "Tests fehlgeschlagen ($platform)" -ForegroundColor Red; exit 1 }
    }
}

if ($Arch -in "all", "x64") { Build "x64"   "build64" }
if ($Arch -in "all", "x86") { Build "Win32" "build32" }

if ($Zip) {
    $zipPath = "$root\csvtc.zip"
    Remove-Item $zipPath -ErrorAction SilentlyContinue
    Compress-Archive -Path "$dist\csvtc.wlx*", "$dist\pluginst.inf" -DestinationPath $zipPath
    Write-Host "Installationsarchiv: $zipPath" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Ausgabe in $dist ===" -ForegroundColor Green
Get-ChildItem $dist | Select-Object Name, Length, LastWriteTime
