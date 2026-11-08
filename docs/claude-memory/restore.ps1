# Restore Claude Code's project memory from this backup (MIT).
# Run from PowerShell, wherever the repository is cloned:
#   powershell -ExecutionPolicy Bypass -File docs\claude-memory\restore.ps1
# The memory folder is named after the checkout's path: C:\Projects\Onyx -> C--Projects-Onyx.
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
$name = $repo -replace '[^A-Za-z0-9]', '-'
$dest = Join-Path $env:USERPROFILE ".claude\projects\$name\memory"
New-Item -ItemType Directory -Force $dest | Out-Null
Get-ChildItem $here -Filter *.md | Where-Object { $_.Name -ne 'README.md' } | Copy-Item -Destination $dest
Write-Host "Memory restored into $dest ($((Get-ChildItem $dest).Count) files)"
