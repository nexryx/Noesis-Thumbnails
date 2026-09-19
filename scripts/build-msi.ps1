param(
    [string]$Version = '1.0.0',
    [string]$NoesisArchive = (Join-Path (Split-Path $PSScriptRoot -Parent) 'noesisv4474.zip'),
    [string]$WixDirectory,
    [string]$Zig,
    [switch]$SkipNativeBuild
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if ($Version -notmatch '^\d{1,3}\.\d{1,3}\.\d{1,5}$') { throw 'Use an MSI version such as 1.0.0' }
$versionParts = $Version.Split('.')
if ([int]$versionParts[0] -gt 255 -or [int]$versionParts[1] -gt 255 -or [int]$versionParts[2] -gt 65535) { throw 'MSI version components must be <= 255.255.65535' }
$archive = (Resolve-Path -LiteralPath $NoesisArchive).Path
$build = Join-Path $root 'build\msi'
$stage = Join-Path $build 'payload'
if (Test-Path -LiteralPath $build) {
    $resolved = (Resolve-Path -LiteralPath $build).Path
    $expected = [IO.Path]::GetFullPath((Join-Path $root 'build\msi'))
    if ($resolved -ne $expected -or -not $resolved.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe MSI build cleanup path' }
    if ((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Build directory may not be a junction' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Force $stage | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $stage 'noesis')
if (-not (Test-Path -LiteralPath (Join-Path $stage 'noesis\Noesis64.exe'))) { throw 'Archive must contain Noesis64.exe at its root' }
if (-not $SkipNativeBuild) {
    $sdk = Join-Path $root '.local\noesis-sdk'
    if (-not (Test-Path -LiteralPath (Join-Path $sdk 'pluginshare.h'))) {
        Expand-Archive -LiteralPath (Join-Path $stage 'noesis\pluginsource.zip') -DestinationPath $sdk -Force
    }
    & (Join-Path $PSScriptRoot 'build.ps1') -Bootstrap -NoesisSdk $sdk -Zig $Zig
}
if (-not $Zig) {
    $Zig = Join-Path $root '.tools\zig-x86_64-windows-0.14.1\zig.exe'
    if (-not (Test-Path -LiteralPath $Zig)) { $Zig = (Get-Command zig -ErrorAction Stop).Source }
}
& $zig c++ '-target' 'x86_64-windows-gnu' '-std=c++17' '-O2' '-shared' '-static' '-DUNICODE' '-D_UNICODE' "-I$(Join-Path $root 'src')" (Join-Path $root 'installer\custom_actions.cpp') '-lmsi' '-ladvapi32' '-lshell32' '-lole32' '-lbcrypt' '-lgdi32' '-luuid' '-o' (Join-Path $build 'InstallerActions.dll')
if ($LASTEXITCODE -ne 0) { throw 'MSI custom actions build failed' }
foreach ($name in @('NoesisThumbnailProvider.dll','noesis-thumbnails.exe')) {
    Copy-Item -LiteralPath (Join-Path $root "dist\$name") -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'noesis\tool_noesis_thumbnails.py') -Destination (Join-Path $stage 'noesis\plugins\python')
Copy-Item -LiteralPath (Join-Path $root 'dist\noesis_thumbnails_formats_x86_64.dll') -Destination (Join-Path $stage 'noesis\plugins\x64\noesis_thumbnails_formats.dll')
Copy-Item -LiteralPath (Join-Path $root 'dist\noesis_thumbnails_formats_x86.dll') -Destination (Join-Path $stage 'noesis\plugins\noesis_thumbnails_formats.dll')
[IO.File]::WriteAllText((Join-Path $stage 'noesis-thumbnails.ini'),"[noesis]`r`nexecutable=noesis\Noesis64.exe`r`n",[Text.Encoding]::Unicode)
# Inventory comes from this exact clean bundled runtime, never a developer's plugins.
$formats = @(& (Join-Path $PSScriptRoot 'discover-formats.ps1') -BinDirectory $stage)
$extensions = @($formats | Where-Object { $_.Image -or $_.Model } | ForEach-Object Extension)
$extensions += @($extensions | ForEach-Object { [IO.Path]::GetExtension('asset'+$_) })
$extensions = @($extensions | Sort-Object -Unique)
[IO.File]::WriteAllText((Join-Path $stage 'extensions.txt'),($extensions -join "`r`n")+"`r`n",(New-Object Text.UTF8Encoding $false))
$guide = (Get-Content -LiteralPath (Join-Path $root 'installer\user-guide.txt') -Raw).Replace('@VERSION@',$Version)
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'),$guide,(New-Object Text.UTF8Encoding $false))
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'installer\THIRD-PARTY-NOTICES.txt') -Destination $stage
# Runtime can regenerate bytecode. Do not ship files generated during discovery.
Get-ChildItem -LiteralPath $stage -Recurse -File -Filter '*.pyc' | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
if (-not $WixDirectory) {
    $WixDirectory = Join-Path $root '.tools\wix314'
    if (-not (Test-Path -LiteralPath (Join-Path $WixDirectory 'candle.exe'))) {
        $zip = Join-Path $root '.tools\wix314-binaries.zip'
        & curl.exe --fail --location --silent --show-error 'https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip' --output $zip
        if ($LASTEXITCODE -ne 0) { throw 'WiX download failed; provide -WixDirectory pointing to WiX 3.14.1 binaries' }
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne '6AC824E1642D6F7277D0ED7EA09411A508F6116BA6FAE0AA5F2C7DAA2FF43D31') { throw 'WiX checksum mismatch' }
        Expand-Archive -LiteralPath $zip -DestinationPath $WixDirectory
    }
}
function Escape-Xml([string]$Value) { [Security.SecurityElement]::Escape($Value) }
function Stable-Id([string]$Name,[string]$Prefix) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $bytes = $algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($Name.ToLowerInvariant())); return $Prefix+([BitConverter]::ToString($bytes).Replace('-','').Substring(0,32)) }
    finally { $algorithm.Dispose() }
}
function Stable-Guid([string]$Name) {
    $id = Stable-Id $Name ''
    return ([Guid]::ParseExact($id,'N')).ToString('D').ToUpperInvariant()
}
$xml = New-Object Text.StringBuilder
[void]$xml.AppendLine('<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi"><Fragment><DirectoryRef Id="INSTALLFOLDER">')
$components = New-Object 'Collections.Generic.List[string]'
function Write-PayloadDirectory([string]$Directory,[string]$Relative) {
    foreach ($file in Get-ChildItem -LiteralPath $Directory -File | Sort-Object Name) {
        $relativeFile = if ($Relative) { "$Relative\$($file.Name)" } else { $file.Name }
        $id = Stable-Id $relativeFile 'C'
        $fileId = Stable-Id $relativeFile 'F'
        $guid = Stable-Guid $relativeFile
        [void]$xml.AppendLine("<Component Id=`"$id`" Guid=`"$guid`" Win64=`"yes`">")
        # HKCU key paths make repair correct for per-user files (ICE38).
        [void]$xml.AppendLine("<RegistryValue Root=`"HKCU`" Key=`"Software\NoesisThumbnails\Files`" Name=`"$id`" Type=`"integer`" Value=`"1`" KeyPath=`"yes`" />")
        [void]$xml.AppendLine("<File Id=`"$fileId`" Name=`"$(Escape-Xml $file.Name)`" Source=`"$(Escape-Xml $file.FullName)`" />")
        [void]$xml.AppendLine("<RemoveFolder Id=`"R$id`" On=`"uninstall`" /></Component>")
        $components.Add($id)
    }
    foreach ($childDirectory in Get-ChildItem -LiteralPath $Directory -Directory | Sort-Object Name) {
        $relativeDir = if ($Relative) { "$Relative\$($childDirectory.Name)" } else { $childDirectory.Name }
        $id = Stable-Id $relativeDir 'D'
        [void]$xml.AppendLine("<Directory Id=`"$id`" Name=`"$(Escape-Xml $childDirectory.Name)`">")
        $directoryComponent = Stable-Id "directory:$relativeDir" 'C'
        $directoryGuid = Stable-Guid "directory:$relativeDir"
        [void]$xml.AppendLine("<Component Id=`"$directoryComponent`" Guid=`"$directoryGuid`" Win64=`"yes`"><RegistryValue Root=`"HKCU`" Key=`"Software\NoesisThumbnails\Files`" Name=`"$directoryComponent`" Type=`"integer`" Value=`"1`" KeyPath=`"yes`" /><RemoveFolder Id=`"R$directoryComponent`" On=`"uninstall`" /></Component>")
        $components.Add($directoryComponent)
        Write-PayloadDirectory $childDirectory.FullName $relativeDir
        [void]$xml.AppendLine('</Directory>')
    }
}
Write-PayloadDirectory $stage ''
[void]$xml.AppendLine('</DirectoryRef></Fragment><Fragment><ComponentGroup Id="Payload">')
foreach ($id in $components) { [void]$xml.AppendLine("<ComponentRef Id=`"$id`" />") }
[void]$xml.AppendLine('</ComponentGroup></Fragment></Wix>')
$payloadWxs = Join-Path $build 'Payload.wxs'
[IO.File]::WriteAllText($payloadWxs,$xml.ToString(),(New-Object Text.UTF8Encoding $false))
$candle = Join-Path $WixDirectory 'candle.exe'; $light = Join-Path $WixDirectory 'light.exe'
& $candle '-nologo' '-arch' 'x64' "-dVersion=$Version" "-dActionsDll=$(Join-Path $build 'InstallerActions.dll')" "-dNoticeRtf=$(Join-Path $root 'installer\notice.rtf')" '-out' "$build\" (Join-Path $root 'installer\Product.wxs') $payloadWxs
if ($LASTEXITCODE -ne 0) { throw 'WiX compile failed' }
$releases = Join-Path $root 'releases'
New-Item -ItemType Directory -Force $releases | Out-Null
$output = Join-Path $releases "NoesisThumbnails-$Version-x64.msi"
# ICE91 warns about per-user directories in packages that could be per-machine;
# this package is deliberately per-user only. ICE61 warns about our deliberate
# same-version upgrade support. All other ICE validation remains enabled.
& $light '-nologo' '-sice:ICE91' '-sice:ICE61' '-ext' (Join-Path $WixDirectory 'WixUIExtension.dll') '-cultures:en-us' '-out' $output (Join-Path $build 'Product.wixobj') (Join-Path $build 'Payload.wixobj')
if ($LASTEXITCODE -ne 0) { throw 'WiX link/validation failed' }
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$output.sha256","$hash  $([IO.Path]::GetFileName($output))`r`n",[Text.Encoding]::ASCII)
Write-Host "MSI ready: $output"
Write-Host "Bundled $(@(Get-ChildItem -LiteralPath $stage -File -Recurse).Count) files; $($extensions.Count) thumbnail extensions; SHA256 $hash"
