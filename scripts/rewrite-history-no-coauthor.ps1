$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$g = "C:\Program Files\Git\mingw64\bin\git.exe"

function Get-CommitField($hash, $format) {
    return (& $g log -1 --format=$format $hash).Trim()
}

$commits = & $g rev-list --reverse HEAD
$map = @{}
foreach ($old in $commits) {
    $tree = (& $g rev-parse "${old}^{tree}").Trim()
    $parentLine = (& $g log -1 --format=%P $old).Trim()
    $parentHashes = @()
    if ($parentLine.Length -gt 0) {
        $parentHashes = $parentLine -split '\s+'
    }

    $env:GIT_AUTHOR_NAME = Get-CommitField $old "%an"
    $env:GIT_AUTHOR_EMAIL = Get-CommitField $old "%ae"
    $env:GIT_AUTHOR_DATE = Get-CommitField $old "%aI"
    $env:GIT_COMMITTER_NAME = Get-CommitField $old "%cn"
    $env:GIT_COMMITTER_EMAIL = Get-CommitField $old "%ce"
    $env:GIT_COMMITTER_DATE = Get-CommitField $old "%cI"

    $msg = Get-CommitField $old "%B"
    $lines = $msg -split "`r?`n"
    $clean = ($lines | Where-Object { $_ -notmatch '^Co-authored-by:\s*Cursor\s*<' }) -join "`n"
    $msgFile = Join-Path $env:TEMP "nerva-commit-msg.txt"
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($msgFile, $clean.TrimEnd() + "`n", $utf8NoBom)

    $args = @("commit-tree", $tree)
    foreach ($p in $parentHashes) {
        $newParent = $map[$p]
        if (-not $newParent) {
            throw "Missing mapped parent for $p while rewriting $old"
        }
        $args += @("-p", $newParent)
    }
    $args += @("-F", $msgFile)
    $new = (& $g @args).Trim()
    Remove-Item $msgFile -Force -ErrorAction SilentlyContinue
    $map[$old] = $new
    Write-Host "$($old.Substring(0,7)) -> $($new.Substring(0,7))"
}

$newHead = $map[$commits[-1]]
& $g update-ref refs/heads/main $newHead
Write-Host "Updated main -> $newHead"
