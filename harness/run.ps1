# CsvTC — Testlauf mit dem Test-Host (ohne Total Commander)
#
# Beispiel:
#   .\harness\run.ps1 -File .\testdata\kunden.csv -Flags 128 -Actions @(
#       "--at 800 key 0x28", "--at 1200 shot {out}\nach_unten.png", "--at 2000 exit")
#
# Aktionen siehe harness.cpp. {out} wird durch den Ausgabeordner ersetzt.
# Die Datei wird vorher nach <Out> kopiert, damit Speichern das Original nicht ändert.
# Einstellungen landen in <Out>\lsplugin.ini (nicht im TC-Ordner).

param(
    [Parameter(Mandatory)][string]$File,
    [string[]]$Actions = @("--at 2000 exit"),
    [ValidateSet("64", "32")][string]$Arch = "64",
    [string]$Out = "$PSScriptRoot\..\testdata\out",
    [string]$Size = "1100x700",
    [int]$Flags = 0,
    [switch]$NoCopy
)

$root = Resolve-Path "$PSScriptRoot\.."
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$exe = if ($Arch -eq "64") { "$root\build64\bin\harness.wlx64.exe" } else { "$root\build32\bin\harness.wlx.exe" }
$dll = if ($Arch -eq "64") { "$root\dist\csvtc.wlx64" } else { "$root\dist\csvtc.wlx" }

$target = (Resolve-Path $File).Path
if (-not $NoCopy) {
    $copy = Join-Path $Out (Split-Path $target -Leaf)
    Copy-Item $target $copy -Force
    $target = $copy
}

$argv = @($dll, $target, "--ini", "$Out\lsplugin.ini", "--log", "$Out\host.log", "--size", $Size, "--flags", $Flags)
foreach ($a in $Actions) {
    foreach ($p in ($a -split ' ', 4)) { $argv += ($p -replace '\{out\}', $Out) }
}
$quoted = $argv | ForEach-Object { if ("$_" -match '\s') { "`"$_`"" } else { "$_" } }
$proc = Start-Process -FilePath $exe -ArgumentList $quoted -PassThru -Wait

"exit: 0x{0:X8}" -f $proc.ExitCode
"--- host"
Get-Content "$Out\host.log"
"--- datei: $target"
