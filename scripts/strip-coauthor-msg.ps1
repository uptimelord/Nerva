$text = [Console]::In.ReadToEnd()
$lines = $text -split "`r?`n"
$filtered = $lines | Where-Object { $_ -notmatch '^Co-authored-by:\s*Cursor\s*<' }
($filtered -join "`n").TrimEnd() + "`n"
