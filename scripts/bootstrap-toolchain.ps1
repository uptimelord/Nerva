param(
    [ValidateSet("auto", "copy", "download", "junction")]
    [string]$Method = "auto",
    [switch]$Global
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "toolchain.ps1")

$gcc = Install-NervaToolchain -Method $Method -Global:$Global
Write-Host "gcc: $gcc"
