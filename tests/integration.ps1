param([Parameter(Mandatory)][string]$NoesisPath,[switch]$ShellTest,[switch]$TimeoutTest)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'dist\noesis-thumbnails.exe'
$noesis = (Resolve-Path -LiteralPath $NoesisPath).Path
if (Test-Path -LiteralPath $noesis -PathType Leaf) { $noesis = Split-Path $noesis -Parent }
$bridgeFixture = Join-Path $noesis 'plugins\python\fmt_nttest.py'
if (Test-Path -LiteralPath $bridgeFixture) { throw 'Test plugin already exists; refusing to overwrite it' }
$testRoot = Join-Path $root ('.local\integration-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $testRoot | Out-Null
$oldCache = $env:NT_CACHE_DIR
$env:NT_CACHE_DIR = Join-Path $testRoot 'cache'
$registered = $false
function Run-Ok([string[]]$Arguments) {
    & $exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Arguments" }
}
try {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fmt_nttest.py') -Destination $bridgeFixture
    & (Join-Path $root 'scripts\configure.ps1') -NoesisPath $noesis
    $formats = @(& (Join-Path $root 'scripts\discover-formats.ps1'))
    foreach ($extension in @('.nttest','.png','.dds','.obj','.fbx')) {
        if ($formats.Extension -notcontains $extension) { throw "Inventory missed $extension" }
    }
    $source = Join-Path $testRoot 'triangle.nttest'
    [IO.File]::WriteAllText($source,'NTST triangle')
    & $exe probe $source
    if ($LASTEXITCODE -ne 2) { throw 'Cold provider should return E_PENDING immediately' }
    Run-Ok @('thumbnail',$source,(Join-Path $testRoot 'triangle.bmp'))
    Run-Ok @('probe',$source)
    Run-Ok @('thumbnail',$source,(Join-Path $testRoot 'triangle-cached.bmp'))
    if ((Get-FileHash (Join-Path $testRoot 'triangle.bmp')).Hash -ne (Get-FileHash (Join-Path $testRoot 'triangle-cached.bmp')).Hash) { throw 'Cache differs from original' }
    # Identical geometry in a reader's Z-up coordinates must produce the same
    # pixels after the native preview transform, including its matrix convention.
    $zup = Join-Path $testRoot 'zup.nttest'
    [IO.File]::WriteAllText($zup,'NTST ZUP')
    Run-Ok @('thumbnail',$zup,(Join-Path $testRoot 'zup.bmp'))
    if ((Get-FileHash (Join-Path $testRoot 'triangle.bmp')).Hash -ne (Get-FileHash (Join-Path $testRoot 'zup.bmp')).Hash) { throw 'Reader preview rotation changed the expected pixels' }
    $emission = Join-Path $testRoot 'emissive.nttest'
    [IO.File]::WriteAllText($emission,'NTST EMIT')
    $emissionBitmap = Join-Path $testRoot 'emissive.bmp'
    Run-Ok @('thumbnail',$emission,$emissionBitmap)
    $pixels = [IO.File]::ReadAllBytes($emissionBitmap)
    $cyan = 0
    for ($i = [BitConverter]::ToInt32($pixels,10); $i -lt $pixels.Length; $i += 4) {
        if ($pixels[$i] -eq 255 -and $pixels[$i+1] -eq 255 -and $pixels[$i+2] -eq 0 -and $pixels[$i+3] -eq 255) { $cyan++ }
    }
    if ($cyan -lt 100) { throw 'Native material bridge lost the emissive pass or used its alpha as opacity' }
    $unicode = Join-Path $testRoot ('model ' + [char]0x65e5 + [char]0x672c + '.nttest')
    Copy-Item -LiteralPath $source -Destination $unicode
    Run-Ok @('thumbnail',$unicode,(Join-Path $testRoot 'unicode.bmp'))
    if ((Get-FileHash (Join-Path $testRoot 'triangle.bmp')).Hash -ne (Get-FileHash (Join-Path $testRoot 'unicode.bmp')).Hash) { throw 'Preview rotation leaked into the next model' }
    [IO.File]::AppendAllText($source,' modified')
    & $exe probe $source
    if ($LASTEXITCODE -ne 2) { throw 'Source modification should invalidate cache' }
    Run-Ok @('thumbnail',$source,(Join-Path $testRoot 'modified.bmp'))
    $bad = Join-Path $testRoot 'broken.nttest'
    [IO.File]::WriteAllText($bad,'not a model')
    & $exe thumbnail $bad (Join-Path $testRoot 'bad.bmp')
    if ($LASTEXITCODE -ne 1) { throw 'Malformed source should fail' }
    $count = @(Get-ChildItem (Join-Path $env:NT_CACHE_DIR 'images') -Filter '*.fail').Count
    & $exe probe $bad
    if ($LASTEXITCODE -ne 1 -or @(Get-ChildItem (Join-Path $env:NT_CACHE_DIR 'images') -Filter '*.fail').Count -ne $count) { throw 'Failure cooldown did not hold' }
    # Corrupt cached data must trigger regeneration rather than returning garbage.
    Get-ChildItem (Join-Path $env:NT_CACHE_DIR 'images') -Filter '*.ntb' | ForEach-Object { [IO.File]::WriteAllText($_.FullName,'corrupt') }
    & $exe probe $source
    if ($LASTEXITCODE -ne 2) { throw 'Corrupt cache should enqueue a replacement' }
    Run-Ok @('thumbnail',$source,(Join-Path $testRoot 'recovered.bmp'))
    if ($TimeoutTest) {
        $hang = Join-Path $testRoot 'timeout.nttest'
        [IO.File]::WriteAllText($hang,'NTST HANG')
        $watch = [Diagnostics.Stopwatch]::StartNew()
        & $exe thumbnail $hang (Join-Path $testRoot 'timeout.bmp')
        if ($LASTEXITCODE -ne 1 -or $watch.Elapsed.TotalSeconds -gt 25 -or $watch.Elapsed.TotalSeconds -lt 19) { throw 'Worker timeout failed' }
        $restart = Join-Path $testRoot 'restart.nttest'
        [IO.File]::WriteAllText($restart,'NTST restarted')
        Run-Ok @('thumbnail',$restart,(Join-Path $testRoot 'restart.bmp'))
    }
    if ($ShellTest) {
        $state = Join-Path $env:LOCALAPPDATA 'NoesisThumbnails\registration.json'
        if (Test-Path -LiteralPath $state) { throw 'Run ShellTest before installing the provider; it requires unregistered state' }
        $registered = $true
        & (Join-Path $root 'scripts\register.ps1') -Extensions '.nttest'
        # Use a new path to ensure the Windows thumbnail cache is cold.
        $shell = Join-Path $testRoot 'shell.nttest'
        [IO.File]::WriteAllText($shell,'NTST shell')
        & $exe shell-probe $shell
        # Poll the actual shell factory, not the direct DLL path.
        $ok = $false
        for ($attempt=0; $attempt -lt 40; $attempt++) {
            Start-Sleep -Milliseconds 250
            & $exe shell-probe $shell
            if ($LASTEXITCODE -eq 0) { $ok = $true; break }
        }
        if (-not $ok) { throw 'Windows Shell did not obtain the asynchronous thumbnail' }
    }
    Write-Host "PASS: integration. Artifacts: $testRoot"
} finally {
    if ($registered) { & (Join-Path $root 'scripts\unregister.ps1') }
    # Only our private test bridge is removed. Persistent worker expires automatically.
    Remove-Item -LiteralPath $bridgeFixture -ErrorAction SilentlyContinue
    $env:NT_CACHE_DIR = $oldCache
}
