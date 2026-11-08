param(
    [string]$Log = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\67fm.exe"
if (-not (Test-Path $exe)) {
    throw "Build 67fm first: build\67fm.exe was not found."
}
if ([string]::IsNullOrWhiteSpace($Log)) {
    $Log = Join-Path $root "build\selftest.log"
} elseif (-not [System.IO.Path]::IsPathRooted($Log)) {
    $Log = Join-Path $root $Log
}

New-Item -ItemType Directory -Force (Split-Path -Parent $Log) | Out-Null
Remove-Item $Log -Force -ErrorAction SilentlyContinue

$process = Start-Process -FilePath $exe `
    -ArgumentList @("--selftest", "--log", $Log) `
    -Wait -PassThru
if (-not (Test-Path $Log)) {
    throw "Self-test did not write a log."
}
$text = Get-Content $Log -Raw
Write-Output $text.TrimEnd()
if ($process.ExitCode -ne 0) {
    throw "Self-test failed (exit $($process.ExitCode))."
}
if ($text -notmatch "summary passed=(\d+) failed=0") {
    throw "Self-test summary missing or contained failures."
}
