$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$workerPath = Join-Path $root 'dist\noesis-thumbnails.exe'
Get-Process -Name 'noesis-thumbnails' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $workerPath } | ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit(5000) | Out-Null }
$cache = Join-Path $env:LOCALAPPDATA 'NoesisThumbnails'
# Delete only known generated files, never registration backups or arbitrary directories.
foreach ($folder in @('images','queue','work')) {
    $path = Join-Path $cache $folder
    if (Test-Path -LiteralPath $path) {
        Get-ChildItem -LiteralPath $path -File | Where-Object { $_.Extension -in '.ntb','.fail','.job','.tmp','.bin','.txt' -or $_.Name -eq 'stop' } | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
    }
}
# A configuration timestamp change invalidates any old keys still held by a client.
$config = Join-Path $root 'dist\noesis-thumbnails.ini'
if (Test-Path -LiteralPath $config) { (Get-Item -LiteralPath $config).LastWriteTimeUtc = [DateTime]::UtcNow }
Write-Host 'Project cache cleared. Explorer also maintains its own cache; refresh the folder or clear Windows thumbnail cache if needed.'
