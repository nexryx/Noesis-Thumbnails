$ErrorActionPreference = 'Stop'
$clsid = '{53EAB7B8-41B9-462B-90DB-BC16434161C7}'
$slot = '{E357FCCD-A995-4576-B01F-234630154E96}'
$stateFile = Join-Path $env:LOCALAPPDATA 'NoesisThumbnails\registration.json'
if (Test-Path -LiteralPath $stateFile) {
    $state = @(Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json)
    foreach ($entry in $state) {
        if ($entry.Extension -notmatch '^\.[a-z0-9_.-]{1,48}$') { throw 'Invalid saved extension' }
        $path = "Software\Classes\$($entry.Extension)\ShellEx\$slot"
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($path,$true)
        if ($key) {
            if ($key.GetValue('') -eq $clsid) {
                if ($entry.HadDefault) { $key.SetValue('',[string]$entry.Previous) } else { $key.DeleteValue('',$false) }
            }
            $key.Dispose()
        }
    }
    Remove-Item -LiteralPath $stateFile
}
[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree("Software\Classes\CLSID\$clsid",$false)
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class NoesisShellRemoveNotify { [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, IntPtr a, IntPtr b); }'
[NoesisShellRemoveNotify]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
Write-Host 'Noesis Thumbnails unregistered. Previous per-user handlers restored where still owned by this project.'
