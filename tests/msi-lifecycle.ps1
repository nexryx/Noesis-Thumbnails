param([string]$MsiPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $MsiPath) {
    $MsiPath = Join-Path $root 'releases\NoesisThumbnails-1.0.0-x64.msi'
}
$msiPath = (Resolve-Path -LiteralPath $MsiPath).Path
$installFolder = Join-Path $env:LOCALAPPDATA 'Programs\NoesisThumbnails'
$cacheFolder = Join-Path $env:LOCALAPPDATA 'NoesisThumbnails'
if (Test-Path -LiteralPath $installFolder) { throw 'MSI test requires Noesis Thumbnails to be uninstalled first' }
$logFolder = Join-Path $root ('build\msi-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $logFolder | Out-Null
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($msiPath,0)
$view = $database.OpenView("SELECT ``Value`` FROM ``Property`` WHERE ``Property``='ProductCode'")
$view.Execute(); $record = $view.Fetch(); $product = $record.StringData(1); $view.Close()
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) | Out-Null
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) | Out-Null
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) | Out-Null
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer) | Out-Null
$clsid = '{53EAB7B8-41B9-462B-90DB-BC16434161C7}'
$slot = '{E357FCCD-A995-4576-B01F-234630154E96}'
$prior = '{71E8A703-0923-459B-A4F8-59CE46891EAF}'
$replacement = '{B41ED8BB-0F6E-4C40-91B6-1899A555EB32}'
$saved = @()
foreach ($ext in @('.obj','.dae')) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Classes\$ext\ShellEx\$slot")
    $value = $key.GetValue('',$null)
    $saved += [pscustomobject]@{Extension=$ext; Present=($null -ne $value); Value=$value}
    $key.Dispose()
}
function Read-Hook([string]$Extension) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Classes\$Extension\ShellEx\$slot")
    if (-not $key) { return $null }
    try { return $key.GetValue('',$null) } finally { $key.Dispose() }
}
function Set-Hook([string]$Extension,[string]$Value) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Classes\$Extension\ShellEx\$slot")
    $key.SetValue('',$Value); $key.Dispose()
}
function Run-Msi([string]$Action,[string]$Target,[string]$LogName,[int[]]$ExpectedExitCodes = @(0,3010)) {
    $log = Join-Path $logFolder "$LogName.log"
    $process = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\msiexec.exe') -ArgumentList "$Action `"$Target`" /qn /norestart /l*v `"$log`"" -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) { throw "MSI exceeded test timeout. See $log" }
    if ($process.ExitCode -notin $ExpectedExitCodes) { throw "MSI returned $($process.ExitCode). See $log" }
    Write-Host "$LogName : MSI exit $($process.ExitCode)"
}
$installed = $false
try {
    Set-Hook '.obj' $prior
    # Fault injection is applied only to this disposable test copy, not shipped.
    # InstallExecute runs the deferred changes, then Type 19 forces rollback.
    $rollbackMsi = Join-Path $logFolder 'rollback-test.msi'
    Copy-Item -LiteralPath $msiPath -Destination $rollbackMsi
    $rollbackProduct = '{' + [Guid]::NewGuid().ToString().ToUpperInvariant() + '}'
    $engine = New-Object -ComObject WindowsInstaller.Installer
    $rollbackDb = $engine.OpenDatabase($rollbackMsi,1)
    $queries = @(
        "UPDATE ``Property`` SET ``Value``='$rollbackProduct' WHERE ``Property``='ProductCode'",
        "INSERT INTO ``CustomAction`` (``Action``,``Type``,``Target``) VALUES ('NTTestFailure',19,'Expected lifecycle test failure after deferred changes')",
        "INSERT INTO ``InstallExecuteSequence`` (``Action``,``Condition``,``Sequence``) VALUES ('InstallExecute','1',5500)",
        "INSERT INTO ``InstallExecuteSequence`` (``Action``,``Condition``,``Sequence``) VALUES ('NTTestFailure','NOT Installed',5501)"
    )
    foreach ($query in $queries) {
        $queryView = $rollbackDb.OpenView($query); $queryView.Execute(); $queryView.Close()
        [Runtime.InteropServices.Marshal]::FinalReleaseComObject($queryView) | Out-Null
    }
    $rollbackDb.Commit()
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($rollbackDb) | Out-Null
    # Every changed MSI needs its own PackageCode, including disposable test MSIs.
    $summary = $engine.SummaryInformation($rollbackMsi,1)
    $summary.GetType().InvokeMember('Property',[Reflection.BindingFlags]::SetProperty,$null,$summary,@(9,('{' + [Guid]::NewGuid().ToString().ToUpperInvariant() + '}'))) | Out-Null
    $summary.Persist()
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary) | Out-Null
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($engine) | Out-Null
    Run-Msi '/i' $rollbackMsi 'rollback' @(1603)
    if ((Read-Hook '.obj') -ne $prior) { throw 'Failed install did not restore previous handler' }
    if (Test-Path -LiteralPath "HKCU:\Software\Classes\CLSID\$clsid") { throw 'Failed install left COM registration behind' }
    if (Test-Path -LiteralPath 'HKCU:\Software\NoesisThumbnails\HandlerBackup') { throw 'Failed install left handler backup behind' }
    if (Test-Path -LiteralPath (Join-Path $installFolder 'noesis-thumbnails.exe')) { throw 'Failed install left installed executable behind' }
    foreach ($hive in @('HKCU','HKLM')) {
        if (Test-Path -LiteralPath "${hive}:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$rollbackProduct") { throw 'Failed install left an Apps entry behind' }
    }
    Run-Msi '/i' $msiPath 'install'
    $installed = $true
    $arp = @("HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$product", "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$product") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $arp) { throw 'Settings/Apps uninstall entry missing' }
    if ((Get-ItemProperty -LiteralPath $arp).DisplayName -ne 'Noesis Thumbnails') { throw 'Incorrect Apps display name' }
    if ((Read-Hook '.obj') -ne $clsid) { throw 'MSI did not register the provider' }
    $worker = Join-Path $installFolder 'noesis-thumbnails.exe'
    $sample = Join-Path $logFolder 'msi-cube.obj'
    # Generate the fixture in the test log folder; no repository example needed.
    $vertices = @('-1 -1 -1','1 -1 -1','1 1 -1','-1 1 -1','-1 -1 1','1 -1 1','1 1 1','-1 1 1')
    $faces = @('1 3 2','1 4 3','5 6 7','5 7 8','1 2 6','1 6 5','4 8 7','4 7 3','1 5 8','1 8 4','2 3 7','2 7 6')
    [IO.File]::WriteAllLines($sample, @('o Cube') + @($vertices | ForEach-Object { "v $_" }) + @($faces | ForEach-Object { "f $_" }))
    & $worker thumbnail $sample (Join-Path $logFolder 'cube.bmp')
    if ($LASTEXITCODE -ne 0) { throw 'Bundled Noesis did not render the fixture' }
    & $worker shell-probe $sample
    if ($LASTEXITCODE -ne 0) { throw 'Windows Shell failed to read the MSI thumbnail' }
    $backup = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\NoesisThumbnails\HandlerBackup\.obj')
    if (-not $backup) { throw 'Original handler was not backed up' }; $backup.Dispose()
    Run-Msi '/fa' $msiPath 'repair'
    if ((Read-Hook '.obj') -ne $clsid) { throw 'Repair lost the handler' }
    # Another application changing a handler after installation must win.
    Set-Hook '.dae' $replacement
    # Have an active worker and cache while uninstalling.
    $second = Join-Path $logFolder 'second.obj'; Copy-Item -LiteralPath $sample -Destination $second
    & $worker cache $second
    if ($LASTEXITCODE -ne 0) { throw 'Cannot start worker before uninstall' }
    Run-Msi '/x' $product 'uninstall'
    $installed = $false
    if (Test-Path -LiteralPath $arp) { throw 'Apps entry survived uninstall' }
    if (Test-Path -LiteralPath $installFolder) { throw 'Installed runtime or generated bytecode survived uninstall' }
    if (Test-Path -LiteralPath $cacheFolder) { throw 'Project cache survived uninstall' }
    if (Test-Path -LiteralPath "HKCU:\Software\Classes\CLSID\$clsid") { throw 'COM class survived uninstall' }
    if ((Read-Hook '.obj') -ne $prior) { throw 'Previous handler was not restored' }
    if ((Read-Hook '.dae') -ne $replacement) { throw 'Uninstall overwrote a newer thumbnail handler' }
    if (Test-Path -LiteralPath 'HKCU:\Software\NoesisThumbnails\HandlerBackup') { throw 'Registry backup survived uninstall' }
    Write-Host "PASS: MSI rollback, install, bundled rendering, Windows Shell, Apps entry, repair, active-worker uninstall, cache cleanup, handler restoration. Logs: $logFolder"
} finally {
    if ($installed) { Run-Msi '/x' $product 'cleanup-after-failure' }
    foreach ($entry in $saved) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Classes\$($entry.Extension)\ShellEx\$slot")
        if ($entry.Present) { $key.SetValue('',$entry.Value) } else { $key.DeleteValue('',$false) }
        $key.Dispose()
    }
}
