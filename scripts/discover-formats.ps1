param([string]$BinDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist'))
$ErrorActionPreference = 'Stop'
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$ini = Get-Content -LiteralPath (Join-Path $bin 'noesis-thumbnails.ini')
$exe = ($ini | Where-Object { $_ -match '^executable=' } | Select-Object -First 1) -replace '^executable=',''
if ($exe -and -not [IO.Path]::IsPathRooted($exe)) { $exe = Join-Path $bin $exe }
if (-not $exe -or -not (Test-Path -LiteralPath $exe)) { throw 'Configure Noesis first' }
$temporary = Join-Path $bin ('formats-' + [Guid]::NewGuid().ToString('N') + '.tsv')
try {
    $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent) -ArgumentList "?runtool `"Noesis Thumbnails Formats`" `"$temporary`"" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$temporary.stdout" -RedirectStandardError "$temporary.stderr"
    if (-not $process.WaitForExit(20000)) { Stop-Process -Id $process.Id -Force; throw 'Noesis format discovery timed out' }
    if (-not (Test-Path -LiteralPath $temporary)) { throw 'Inventory plugin did not produce a format list' }
    # Noesis initializes Python readers lazily for native tools. Validate again
    # from a Python tool after all Python format registrations have executed.
    $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent) -ArgumentList "?runtool `"Noesis Thumbnails Verify Formats`" `"$temporary`"" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$temporary.stdout" -RedirectStandardError "$temporary.stderr"
    if (-not $process.WaitForExit(20000)) { Stop-Process -Id $process.Id -Force; throw 'Python format verification timed out' }
    $verified = "$temporary.verified"
    if (-not (Test-Path -LiteralPath $verified)) { throw 'Python format validation did not complete' }
    $lines = @(Get-Content -LiteralPath $verified)
    if ($lines[-1] -ne '# NT_FORMATS_COMPLETE') { throw 'Incomplete format inventory' }
    $formats = @($lines | ForEach-Object {
        $parts = $_ -split "`t"
        if ($parts.Count -eq 2 -and $parts[0] -match '^\.[a-zA-Z0-9_.-]{1,48}$') {
            $flags = [int]$parts[1]
            [pscustomobject]@{ Extension=$parts[0].ToLowerInvariant(); Image=[bool]($flags -band 2); Model=[bool]($flags -band 8); Archive=[bool]($flags -band 1); Flags=$flags }
        }
    } | Sort-Object Extension -Unique)
    if ($formats.Count -eq 0) { throw 'Noesis returned no supported formats' }
    $json = ConvertTo-Json -InputObject @($formats) -Depth 4
    [IO.File]::WriteAllText((Join-Path $bin 'formats.json'),$json,(New-Object Text.UTF8Encoding $false))
    $formats | Export-Csv -LiteralPath (Join-Path $bin 'formats.csv') -NoTypeInformation -Encoding UTF8
    $visual = @($formats | Where-Object { $_.Image -or $_.Model })
    Write-Host "Found $($formats.Count) extensions; $($visual.Count) have image/model readers. Inventory: $bin\formats.json"
    return $formats
} finally {
    foreach ($path in @($temporary,"$temporary.verified","$temporary.stdout","$temporary.stderr")) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path } }
}
