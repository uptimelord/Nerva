param(
    [switch]$Bootstrap,
    [switch]$SkipTest
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
. (Join-Path $PSScriptRoot "scripts\toolchain.ps1")

$CFLAGS = @("-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Iinclude", "-Itests")
$LibSrc = @(
    "src/nerva_graph.c",
    "src/nerva_engine.c",
    "src/nerva_event.c",
    "src/nerva_debug.c",
    "src/nerva_trace.c",
    "src/nerva_mutation.c",
    "src/nerva_learning.c",
    "src/nerva_prediction.c",
    "src/nerva_exception.c",
    "src/nerva_schema.c",
    "src/nerva_memory.c",
    "src/nerva_routing.c",
    "src/nerva_parse.c",
    "src/nerva_persist.c",
    "src/nerva_bench.c"
)
$TestSrc = @(
    "tests/test_runner.c",
    "tests/test_graph.c",
    "tests/test_event.c",
    "tests/test_trace.c",
    "tests/test_learning.c",
    "tests/test_prediction.c",
    "tests/test_exception.c",
    "tests/test_schema.c",
    "tests/test_memory.c",
    "tests/test_routing.c",
    "tests/test_parse.c",
    "tests/test_persist.c",
    "tests/test_bench.c",
    "tests/nerva_test_fixtures.c"
)

$build = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $build)) {
    New-Item -ItemType Directory -Path $build | Out-Null
}

$compiler = Resolve-NervaCompiler -Bootstrap:$Bootstrap
if (-not $compiler) {
    Write-Host @"

No C compiler found.

Run once to install a repo-local toolchain (~900 MB, gitignored):
  powershell -ExecutionPolicy Bypass -File scripts\bootstrap-toolchain.ps1

Or install globally (adds to user PATH):
  powershell -ExecutionPolicy Bypass -File scripts\bootstrap-toolchain.ps1 -Global

Or install system-wide via winget:
  winget install BrechtSanders.WinLibs.POSIX.UCRT

"@
    exit 1
}

$out = Join-Path $build "test_runner.exe"

if ($compiler.Kind -eq "gcc") {
    Write-Host "Using $($compiler.Path)"
    $libObjs = @()
    $testObjs = @()
    foreach ($src in $LibSrc) {
        $obj = Join-Path $build ([System.IO.Path]::GetFileName([System.IO.Path]::ChangeExtension($src, ".obj")))
        & $compiler.Path @CFLAGS -c $src -o $obj
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $libObjs += $obj
    }
    foreach ($src in $TestSrc) {
        $obj = Join-Path $build ([System.IO.Path]::GetFileName([System.IO.Path]::ChangeExtension($src, ".obj")))
        & $compiler.Path @CFLAGS -c $src -o $obj
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $testObjs += $obj
    }
    & $compiler.Path @CFLAGS -o $out @libObjs @testObjs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "Using MSVC via $($compiler.Path)"
    $srcList = ($LibSrc + $TestSrc) -join " "
    $cmd = "`"$($compiler.Path)`" >nul && cl /nologo /std:c11 /W3 /O2 /Iinclude /D_CRT_SECURE_NO_WARNINGS $srcList /Fe:$out"
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Build OK"
$cli = Join-Path $build "nerva_cli.exe"
$bench = Join-Path $build "nerva_bench.exe"
if ($compiler.Kind -eq "gcc") {
    & $compiler.Path @CFLAGS -o $cli (Join-Path $PSScriptRoot "tools\nerva_cli.c") @libObjs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $compiler.Path @CFLAGS -o $bench (Join-Path $PSScriptRoot "tools\nerva_bench.c") @libObjs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    $cliSrc = Join-Path $PSScriptRoot "tools\nerva_cli.c"
    $benchSrc = Join-Path $PSScriptRoot "tools\nerva_bench.c"
    $cmd = "`"$($compiler.Path)`" >nul && cl /nologo /std:c11 /W3 /O2 /Iinclude /Itests /D_CRT_SECURE_NO_WARNINGS $cliSrc $(($libObjs) -join ' ') /Fe:$cli"
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $cmd = "`"$($compiler.Path)`" >nul && cl /nologo /std:c11 /W3 /O2 /Iinclude /Itests /D_CRT_SECURE_NO_WARNINGS $benchSrc $(($libObjs) -join ' ') /Fe:$bench"
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not $SkipTest) {
    & $out
    exit $LASTEXITCODE
}
