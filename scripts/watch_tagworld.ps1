param(
    [int]$Episodes = 3,
    [string]$Mode = "action",
    [int]$Seed = 1,
    [switch]$NoOpen
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root

$Exe = Join-Path $Root "build\nerva_tagworld.exe"
$ViewerDir = Join-Path $Root "worlds\tagworld\viewer"
$RunsDir = Join-Path $Root "runs\tagworld"
$Replay = Join-Path $RunsDir "demo.jsonl"
$Index = Join-Path $ViewerDir "index.html"

if (-not (Test-Path $RunsDir)) {
    New-Item -ItemType Directory -Path $RunsDir -Force | Out-Null
}

if (-not (Test-Path $Exe)) {
    Write-Host "Building nerva_tagworld..."
    & (Join-Path $Root "build.ps1") -SkipTest
}

Write-Host "Recording $Episodes episode(s), mode=$Mode, seed=$Seed -> runs/tagworld/demo.jsonl"
& $Exe --episodes $Episodes --mode $Mode --seed $Seed --write-replay $Replay --fast
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Replay written: $Replay"
Copy-Item $Replay (Join-Path $ViewerDir "demo.jsonl") -Force

if (-not $NoOpen) {
    if (-not (Test-Path $Index)) {
        Write-Error "Viewer not found: $Index"
    }

    $port = 8765
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        Write-Host "Starting viewer at http://localhost:$port ..."
        $serverJob = Start-Job -ScriptBlock {
            param($Dir, $Port)
            Set-Location $Dir
            python -m http.server $Port 2>$null
        } -ArgumentList $ViewerDir, $port
        Start-Sleep -Milliseconds 400
        Start-Process "http://localhost:$port"
        Write-Host "Press Ctrl+C to stop the viewer server."
        try {
            Wait-Job $serverJob | Out-Null
        } finally {
            Stop-Job $serverJob -ErrorAction SilentlyContinue
            Remove-Job $serverJob -Force -ErrorAction SilentlyContinue
        }
    } else {
        Write-Host "Opening viewer (use Load replay -> runs/tagworld/demo.jsonl)..."
        Start-Process $Index
    }
}
