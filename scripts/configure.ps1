param([Parameter(Mandatory)][string]$NoesisPath, [string]$BinDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist'))
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $NoesisPath).Path
if (Test-Path -LiteralPath $exe -PathType Container) {
    $folder = $exe
    $exe = Join-Path $folder 'Noesis64.exe'
    if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $folder 'Noesis.exe' }
}
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Noesis executable not found' }
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$workerPath = Join-Path $bin 'noesis-thumbnails.exe'
Get-Process -Name 'noesis-thumbnails' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $workerPath } | ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit(5000) | Out-Null }
$pluginDir = Join-Path (Split-Path $exe -Parent) 'plugins\python'
if (-not (Test-Path -LiteralPath $pluginDir)) { throw 'Noesis plugins/python directory not found' }
$plugin = Join-Path (Split-Path $PSScriptRoot -Parent) 'noesis\tool_noesis_thumbnails.py'
Copy-Item -LiteralPath $plugin -Destination $pluginDir -Force
$is64 = [IO.Path]::GetFileName($exe) -ieq 'Noesis64.exe'
$architecture = if ($is64) { 'x86_64' } else { 'x86' }
$nativeDir = if ($is64) { Join-Path (Split-Path $exe -Parent) 'plugins\x64' } else { Join-Path (Split-Path $exe -Parent) 'plugins' }
$inventory = Join-Path $bin "noesis_thumbnails_formats_$architecture.dll"
if (-not (Test-Path -LiteralPath $inventory)) { throw 'Build the format inventory plugin first' }
Copy-Item -LiteralPath $inventory -Destination (Join-Path $nativeDir 'noesis_thumbnails_formats.dll') -Force
# UTF-16 is understood by GetPrivateProfileStringW, including non-ASCII paths.
[IO.File]::WriteAllText((Join-Path $bin 'noesis-thumbnails.ini'), "[noesis]`r`nexecutable=$exe`r`n", [Text.Encoding]::Unicode)
Write-Host "Configured Noesis: $exe"
Write-Host 'Bridge installed; rerun this script after changing Noesis plugins to invalidate our cache.'
