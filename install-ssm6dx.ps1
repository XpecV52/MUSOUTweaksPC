param([string]$GameDir = 'F:\SteamLibrary\steamapps\common\Dynasty Warriors 7 DX')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$target = (Resolve-Path -LiteralPath $GameDir).Path
$executables = @('SM6TC.exe','SM6EN.exe','SM6JP.exe')
if (-not ($executables | Where-Object { Test-Path -LiteralPath (Join-Path $target $_) })) {
    throw 'No supported SSM6 DX executable (SM6TC.exe, SM6EN.exe, or SM6JP.exe) was found.'
}
$running = Get-Process -Name @('SM6TC','SM6EN','SM6JP') -ErrorAction SilentlyContinue
if ($running) { throw "Exit the SSM6 DX game before installing: $($running.ProcessName -join ', ')" }
$files = @('dinput8.dll','SSM6_Tweaks.ini')
$backupFiles = @($files + 'OrochiZPadX.ini' + 'MUSOUTweaksPC.log')
foreach ($name in $files) {
    $source = if ($name -eq 'dinput8.dll') { Join-Path $root 'build\dinput8.dll' } else { Join-Path $root $name }
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing patch file: $source" }
}
$backup = Join-Path $target ('MUSOUTweaksPC-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$manifest = @()
foreach ($name in $backupFiles) {
    $destination = Join-Path $target $name
    $existed = Test-Path -LiteralPath $destination
    if ($existed) { Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $name) -Force }
    $manifest += [pscustomobject]@{name=$name; existed=$existed}
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
try {
    Copy-Item -LiteralPath (Join-Path $root 'build\dinput8.dll') -Destination (Join-Path $target 'dinput8.dll') -Force
    $config = Join-Path $target 'SSM6_Tweaks.ini'
    if (-not (Test-Path -LiteralPath $config)) {
        Copy-Item -LiteralPath (Join-Path $root 'SSM6_Tweaks.ini') -Destination $config
    }
    $legacyConfig = Join-Path $target 'OrochiZPadX.ini'
    if (Test-Path -LiteralPath $legacyConfig) { Remove-Item -LiteralPath $legacyConfig }
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
Write-Output 'Launch the game normally. The patch supports XInput, native DS4, D-pad mapping, hot-plug, and borderless desktop-resolution rendering.'
