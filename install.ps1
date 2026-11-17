param([string]$GameDir = 'E:\Games\Non-Steam\Musou Orochi Z')
$ErrorActionPreference = 'Stop'
$target = (Resolve-Path -LiteralPath $GameDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $target 'OROCHI_Z_TC.exe'))) { throw 'Game executable was not found.' }
if (Get-Process -Name OROCHI_Z_TC -ErrorAction SilentlyContinue) { throw 'Exit OROCHI_Z_TC normally before installing.' }
$files = @('dinput8.dll', 'OrochiZPadX.ini', 'OrochiZPadX-diagnose.exe')
foreach ($name in $files) {
    $source = if ($name -eq 'OrochiZPadX.ini') { Join-Path $PSScriptRoot $name } else { Join-Path $PSScriptRoot "build\$name" }
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing build file: $source" }
}
$backup = Join-Path $target ('MUSOUTweaksPC-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$manifest = @()
foreach ($name in $files) {
    $destination = Join-Path $target $name
    $existed = Test-Path -LiteralPath $destination
    if ($existed) { Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $name) }
    $manifest += [pscustomobject]@{name=$name; existed=$existed}
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
try {
    foreach ($name in $files) {
        $source = if ($name -eq 'OrochiZPadX.ini') { Join-Path $PSScriptRoot $name } else { Join-Path $PSScriptRoot "build\$name" }
        Copy-Item -LiteralPath $source -Destination (Join-Path $target $name) -Force
    }
} catch {
    foreach ($entry in $manifest) {
        $destination = Join-Path $target $entry.name
        if ($entry.existed) { Copy-Item -LiteralPath (Join-Path $backup $entry.name) -Destination $destination -Force }
        elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
    }
    throw
}
Write-Output "Installed in: $target"
Write-Output "Backup: $backup"
Write-Output 'Launch the game normally. Existing 1.2 controller bindings are compatible.'
