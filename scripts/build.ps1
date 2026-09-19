param([string]$Zig, [switch]$Bootstrap, [string]$NoesisSdk)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Zig) {
    $local = Join-Path $root '.tools\zig-x86_64-windows-0.14.1\zig.exe'
    if (Test-Path -LiteralPath $local) { $Zig = $local }
    elseif (Get-Command zig -ErrorAction SilentlyContinue) { $Zig = (Get-Command zig).Source }
    elseif ($Bootstrap) {
        $toolsDir = Join-Path $root '.tools'
        New-Item -ItemType Directory -Force $toolsDir | Out-Null
        $archive = Join-Path $toolsDir 'zig-0.14.1.zip'
        & curl.exe --fail --location --silent --show-error 'https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip' --output $archive
        if ($LASTEXITCODE -ne 0) { throw 'Zig download failed' }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c') { throw 'Zig checksum mismatch' }
        Expand-Archive -LiteralPath $archive -DestinationPath $toolsDir -Force
        $Zig = $local
    } else { throw 'Install Zig 0.14.1, pass -Zig, or use -Bootstrap.' }
}
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
if (-not $NoesisSdk) { $NoesisSdk = Join-Path $root '.local\noesis-sdk' }
if (-not (Test-Path -LiteralPath (Join-Path $NoesisSdk 'pluginshare.h'))) {
    $sdkZip = Join-Path $root '.local\noesis\pluginsource.zip'
    if (-not (Test-Path -LiteralPath $sdkZip)) { throw 'Extract Noesis/pluginsource.zip and pass its folder as -NoesisSdk.' }
    Expand-Archive -LiteralPath $sdkZip -DestinationPath $NoesisSdk -Force
}
# Stop only this build's broker; its Job Object also closes its private Noesis child.
$workerPath = Join-Path $dist 'noesis-thumbnails.exe'
Get-Process -Name 'noesis-thumbnails' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $workerPath } | ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit(5000) | Out-Null }
$common = @('-target','x86_64-windows-gnu','-std=c++17','-O2','-DNDEBUG','-DUNICODE','-D_UNICODE','-Isrc','-lole32','-lshell32','-lshlwapi','-lbcrypt','-lgdi32','-luuid','-static')
Push-Location $root
try {
    & $Zig c++ @common '-shared' 'src/provider.cpp' 'src/provider.def' '-o' 'dist/NoesisThumbnailProvider.dll'
    if ($LASTEXITCODE -ne 0) { throw 'Provider build failed' }
    & $Zig c++ @common '-municode' 'src/worker.cpp' '-o' 'dist/noesis-thumbnails.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Worker build failed' }
    & $Zig c++ @common '-municode' 'tests/native_tests.cpp' '-o' 'dist/native-tests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
    & '.\dist\native-tests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
    foreach ($arch in @('x86_64','x86')) {
        $extra = @()
        if ($arch -eq 'x86_64') { $extra += '-D_NOE64' }
        & $Zig c++ '-target' "$arch-windows-gnu" '-std=c++17' '-O2' '-shared' '-static' '-fms-extensions' '-Wno-pragma-pack' '-Wno-null-conversion' '-Wno-deprecated-declarations' '-DNOMINMAX' "-I$NoesisSdk" @extra 'noesis/format_inventory.cpp' '-o' "dist/noesis_thumbnails_formats_$arch.dll"
        if ($LASTEXITCODE -ne 0) { throw "Noesis format inventory build failed: $arch" }
    }
    Copy-Item -LiteralPath 'noesis/tool_noesis_thumbnails.py' -Destination $dist -Force
} finally { Pop-Location }
Write-Host "Built and tested: $dist"
