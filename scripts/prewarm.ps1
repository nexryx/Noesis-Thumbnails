param([Parameter(Mandatory)][string]$Directory,[switch]$Recurse)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$formats = @(Get-Content -LiteralPath (Join-Path $root 'dist\formats.json') -Raw | ConvertFrom-Json | Where-Object { $_.Image -or $_.Model })
$extensions = @($formats.Extension | ForEach-Object { [IO.Path]::GetExtension('asset' + $_) } | Sort-Object -Unique)
$failed = 0; $completed = 0
Get-ChildItem -LiteralPath $Directory -File -Recurse:$Recurse | Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() } | ForEach-Object {
    Write-Host $_.FullName
    & (Join-Path $root 'dist\noesis-thumbnails.exe') cache $_.FullName
    if ($LASTEXITCODE -eq 0) { $completed++ } else { $failed++ }
}
Write-Host "Cache ready: $completed; unsupported/failed: $failed"
if ($failed) { exit 1 }
