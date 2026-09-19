param([string]$NoesisPath, [switch]$Build, [string[]]$Extensions)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $NoesisPath) {
    $NoesisPath = Join-Path $root '.local\noesis'
    if (-not (Test-Path -LiteralPath (Join-Path $NoesisPath 'Noesis64.exe'))) {
        $archive = Join-Path $root 'noesisv4474.zip'
        if (-not (Test-Path -LiteralPath $archive)) { throw 'Pass -NoesisPath pointing to your own Noesis installation.' }
        Expand-Archive -LiteralPath $archive -DestinationPath $NoesisPath -Force
    }
}
if ($Build -or -not (Test-Path -LiteralPath (Join-Path $root 'dist\noesis-thumbnails.exe'))) {
    $noesisDir = (Resolve-Path -LiteralPath $NoesisPath).Path
    if (Test-Path -LiteralPath $noesisDir -PathType Leaf) { $noesisDir = Split-Path $noesisDir -Parent }
    $sdk = Join-Path $root '.local\noesis-sdk'
    if (-not (Test-Path -LiteralPath (Join-Path $sdk 'pluginshare.h'))) {
        Expand-Archive -LiteralPath (Join-Path $noesisDir 'pluginsource.zip') -DestinationPath $sdk -Force
    }
    & (Join-Path $PSScriptRoot 'build.ps1') -Bootstrap -NoesisSdk $sdk
}
& (Join-Path $PSScriptRoot 'configure.ps1') -NoesisPath $NoesisPath
& (Join-Path $PSScriptRoot 'register.ps1') -Extensions $Extensions
