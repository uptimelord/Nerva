$ErrorActionPreference = "Stop"

function Get-NervaRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-NervaToolchainDir {
    param([string]$Root = (Get-NervaRoot))
    return Join-Path $Root "toolchain\mingw64"
}

function Get-NervaGccPath {
    param([string]$Root = (Get-NervaRoot))

    $local = Join-Path (Get-NervaToolchainDir -Root $Root) "bin\gcc.exe"
    if (Test-Path $local) {
        return $local
    }

    $globalDir = Join-Path ${env:ProgramFiles} "Nerva\toolchain\mingw64\bin\gcc.exe"
    if (Test-Path $globalDir) {
        return $globalDir
    }

    $wingetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    if (Test-Path $wingetRoot) {
        $wingetGcc = Get-ChildItem $wingetRoot -Directory -Filter "BrechtSanders.WinLibs*" -ErrorAction SilentlyContinue |
            ForEach-Object {
                $candidate = Join-Path $_.FullName "mingw64\bin\gcc.exe"
                if (Test-Path $candidate) { $candidate }
            } |
            Select-Object -First 1
        if ($wingetGcc -and (Test-Path $wingetGcc)) {
            return $wingetGcc
        }
    }

    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -notmatch '\\Temp\\WinGet\\') {
        return $cmd.Source
    }

    foreach ($p in @(
            "C:\Program Files\WinLibs\bin\gcc.exe",
            "C:\mingw64\bin\gcc.exe",
            "C:\msys64\mingw64\bin\gcc.exe"
        )) {
        if (Test-Path $p) { return $p }
    }

    return $null
}

function Find-WingetWinLibsRoot {
    $wingetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    if (-not (Test-Path $wingetRoot)) {
        return $null
    }

    $known = Get-ChildItem $wingetRoot -Directory -Filter "BrechtSanders.WinLibs*" -ErrorAction SilentlyContinue |
        ForEach-Object {
            $candidate = Join-Path $_.FullName "mingw64"
            if (Test-Path (Join-Path $candidate "bin\gcc.exe")) { $candidate }
        } |
        Select-Object -First 1

    if ($known) {
        return (Resolve-Path $known).Path
    }

    $gcc = Get-ChildItem $wingetRoot -Filter gcc.exe -Recurse -Depth 5 -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'WinLibs' -and $_.FullName -notmatch '\\Temp\\' } |
        Select-Object -First 1
    if (-not $gcc) {
        return $null
    }
    return (Resolve-Path $gcc.Directory.Parent.FullName).Path
}

function Read-NervaToolchainVersion {
    param([string]$Root = (Get-NervaRoot))
    $versionFile = Join-Path $Root "toolchain\VERSION"
    if (-not (Test-Path $versionFile)) {
        throw "Missing toolchain/VERSION"
    }
    $lines = Get-Content $versionFile | Where-Object { $_.Trim() -ne "" }
    return @{
        Version = $lines[0].Trim()
        Url     = $lines[1].Trim()
        Sha256  = $lines[2].Trim().ToLowerInvariant()
    }
}

function Install-NervaToolchain {
    param(
        [ValidateSet("auto", "copy", "download", "junction")]
        [string]$Method = "auto",
        [string]$Root = (Get-NervaRoot),
        [switch]$Global
    )

    $destRoot = if ($Global) {
        Join-Path ${env:ProgramFiles} "Nerva\toolchain\mingw64"
    } else {
        Get-NervaToolchainDir -Root $Root
    }

    $destGcc = Join-Path $destRoot "bin\gcc.exe"
    if (Test-Path $destGcc) {
        Write-Host "Toolchain already present at $destRoot"
        return $destGcc
    }

    $parent = Split-Path $destRoot -Parent
    if (-not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    if ($Method -eq "auto") {
        $source = Find-WingetWinLibsRoot
        if ($source) {
            $Method = "junction"
        } else {
            $Method = "download"
        }
    }

    if ($Method -eq "junction") {
        $source = Find-WingetWinLibsRoot
        if (-not $source) {
            throw "No local WinLibs install found for junction. Use -Method download or run: winget install BrechtSanders.WinLibs.POSIX.UCRT"
        }
        if (Test-Path $destRoot) {
            Remove-Item $destRoot -Recurse -Force
        }
        Write-Host "Linking $destRoot -> $source"
        cmd.exe /c "mklink /J `"$destRoot`" `"$source`""
        if ($LASTEXITCODE -ne 0) {
            throw "mklink failed with exit code $LASTEXITCODE"
        }
    } elseif ($Method -eq "copy") {
        $source = Find-WingetWinLibsRoot
        if (-not $source) {
            throw "No local WinLibs install found to copy. Use -Method download or run: winget install BrechtSanders.WinLibs.POSIX.UCRT"
        }
        Write-Host "Copying WinLibs from $source to $destRoot ..."
        if (Test-Path $destRoot) {
            Remove-Item $destRoot -Recurse -Force
        }
        & robocopy $source $destRoot /E /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
        if ($LASTEXITCODE -ge 8) {
            throw "robocopy failed with exit code $LASTEXITCODE"
        }
    } else {
        $meta = Read-NervaToolchainVersion -Root $Root
        $zipPath = Join-Path $env:TEMP "nerva-winlibs-$($meta.Version).zip"
        $extractRoot = Join-Path $env:TEMP "nerva-winlibs-$($meta.Version)"

        Write-Host "Downloading WinLibs $($meta.Version) ..."
        Invoke-WebRequest -Uri $meta.Url -OutFile $zipPath -UseBasicParsing

        $hash = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $meta.Sha256) {
            throw "WinLibs SHA256 mismatch. Expected $($meta.Sha256), got $hash"
        }

        if (Test-Path $extractRoot) {
            Remove-Item $extractRoot -Recurse -Force
        }
        Expand-Archive -Path $zipPath -DestinationPath $extractRoot

        $inner = Get-ChildItem $extractRoot -Directory | Where-Object { Test-Path (Join-Path $_.FullName "bin\gcc.exe") } | Select-Object -First 1
        if (-not $inner) {
            throw "Extracted archive does not contain mingw64/bin/gcc.exe"
        }

        if (Test-Path $destRoot) {
            Remove-Item $destRoot -Recurse -Force
        }
        Move-Item -Path $inner.FullName -Destination $destRoot
        Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
        Remove-Item $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path $destGcc)) {
        throw "Toolchain install failed: $destGcc not found"
    }

    if ($Global) {
        $binDir = Join-Path $destRoot "bin"
        $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
        if ($userPath -notlike "*$binDir*") {
            [Environment]::SetEnvironmentVariable("Path", "$userPath;$binDir", "User")
            Write-Host "Added $binDir to user PATH (restart shell to pick up globally)."
        }
    }

    Write-Host "Toolchain ready at $destRoot"
    return $destGcc
}

function Find-VcVars {
    foreach ($p in @(
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
            "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        )) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Resolve-NervaCompiler {
    param(
        [string]$Root = (Get-NervaRoot),
        [switch]$Bootstrap
    )

    $gcc = Get-NervaGccPath -Root $Root
    if ($gcc) {
        return @{ Kind = "gcc"; Path = $gcc }
    }

    if ($Bootstrap) {
        $gcc = Install-NervaToolchain -Root $Root
        return @{ Kind = "gcc"; Path = $gcc }
    }

    $vcvars = Find-VcVars
    if ($vcvars) {
        return @{ Kind = "msvc"; Path = $vcvars }
    }

    return $null
}
