param([string]$GameDir = 'E:\Games\Non-Steam\Shin Sangokumusou 5')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$target = (Resolve-Path -LiteralPath $GameDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $target 'SM5_WIN.exe'))) {
    throw 'SM5_WIN.exe was not found. Point -GameDir at the folder that holds it.'
}
$running = Get-Process -Name @('SM5_WIN') -ErrorAction SilentlyContinue
if ($running) { throw "Exit the game before installing: $($running.ProcessName -join ', ')" }
$installFiles = @([pscustomobject]@{ source='build\dinput8.dll'; name='dinput8.dll' })
$configPath = Join-Path $target 'SSM5_Tweaks.ini'
if (-not (Test-Path -LiteralPath $configPath)) {
    $installFiles += [pscustomobject]@{ source='SSM5_Tweaks.ini'; name='SSM5_Tweaks.ini' }
}
foreach ($item in $installFiles) {
    $source = Join-Path $root $item.source
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing patch file: $source" }
}
$backupFiles = @('dinput8.dll','SSM5_Tweaks.ini','OrochiZPadX.ini','MUSOUTweaksPC.log')
$backup = Join-Path $target ('MUSOUTweaksPC-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$manifest = @()
foreach ($name in $backupFiles) {
    $destination = Join-Path $target $name
    $existed = Test-Path -LiteralPath $destination
    if ($existed) { Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $name) -Force }
    $manifest += [pscustomobject]@{ name = $name; existed = $existed }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
try {
    foreach ($item in $installFiles) {
        Copy-Item -LiteralPath (Join-Path $root $item.source) -Destination (Join-Path $target $item.name) -Force
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
Write-Output 'Launch the game normally. The patch supplies a virtual controller named "Controller (XBOX 360 For Windows)" because DW6 PC only accepts the pad names on its built-in list.'
