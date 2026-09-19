param([string[]]$Extensions, [string]$BinDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist'))
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run from 64-bit PowerShell' }
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$dll = Join-Path $bin 'NoesisThumbnailProvider.dll'
foreach ($required in @($dll,(Join-Path $bin 'noesis-thumbnails.exe'),(Join-Path $bin 'noesis-thumbnails.ini'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Build and configure first: $required" }
}
if (-not $Extensions) {
    $formats = @(& (Join-Path $PSScriptRoot 'discover-formats.ps1') -BinDirectory $bin)
    $Extensions = @($formats | Where-Object { $_.Image -or $_.Model } | ForEach-Object { $_.Extension })
    # Windows associates the final suffix of compound names (for example .mesh.1808312334).
    $Extensions += @($Extensions | ForEach-Object { [IO.Path]::GetExtension('asset' + $_) })
}
$Extensions = @($Extensions | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique)
foreach ($ext in $Extensions) { if ($ext -notmatch '^\.[a-z0-9_.-]{1,48}$') { throw "Invalid extension: $ext" } }
$clsid = '{53EAB7B8-41B9-462B-90DB-BC16434161C7}'
$slot = '{E357FCCD-A995-4576-B01F-234630154E96}'
$stateDir = Join-Path $env:LOCALAPPDATA 'NoesisThumbnails'
New-Item -ItemType Directory -Force $stateDir | Out-Null
$stateFile = Join-Path $stateDir 'registration.json'
$state = @()
if (Test-Path -LiteralPath $stateFile) { $state = @(Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json) }
# Save the prior per-user handler before modifying it, so uninstall is reversible.
foreach ($ext in $Extensions) {
    if ($state.Extension -contains $ext) { continue }
    $path = "Software\Classes\$ext\ShellEx\$slot"
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($path)
    $previous = if ($key) { $key.GetValue('',$null) } else { $null }
    $state += [pscustomobject]@{ Extension=$ext; HadDefault=($null -ne $previous); Previous=$previous }
    if ($key) { $key.Dispose() }
}
$json = ConvertTo-Json -InputObject @($state) -Depth 5
[IO.File]::WriteAllText($stateFile,$json,(New-Object Text.UTF8Encoding $false))
$class = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Classes\CLSID\$clsid")
$class.SetValue('','Noesis Thumbnails')
# File-based initialization is necessary to retain sibling textures and paired game assets.
# The DLL only queues work; every Noesis parser runs in a separate process.
$class.SetValue('DisableProcessIsolation',1,[Microsoft.Win32.RegistryValueKind]::DWord)
$server = $class.CreateSubKey('InprocServer32')
$server.SetValue('',$dll); $server.SetValue('ThreadingModel','Apartment'); $server.Dispose(); $class.Dispose()
foreach ($ext in $Extensions) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Classes\$ext\ShellEx\$slot")
    $key.SetValue('',$clsid); $key.Dispose()
}
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class NoesisShellNotify { [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, IntPtr a, IntPtr b); }'
[NoesisShellNotify]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
Write-Host "Registered for current user: $($Extensions -join ', ')"
Write-Host 'Open Explorer in Large/Extra large icons view. Initial renders arrive asynchronously; use F5 if needed.'
