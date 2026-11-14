param(
    [string]$Shin3 = 'E:\Games\Non-Steam\Shin Sangokumusou 3\game',
    [string]$Shin4 = 'E:\Games\Non-Steam\Shin Sangokumusou 4',
    [string]$Sengoku2 = 'E:\Games\Non-Steam\Sengoku Musou 2',
    [string]$Dynasty8 = 'F:\SteamLibrary\steamapps\common\Dynasty Warriors 8'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$patchedGames = @('ShinSangokumusou3','SM4_WIN','SS4','SS4_Plus','SS4_Super','SS4_Ultima','Launch')
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in $patchedGames }
if ($running) { throw "Exit patched games before installing: $($running.ProcessName -join ', ')" }
$targets = @(
    @{ dir = $Shin3; dll = 'build\dinput8.dll'; ini = 'SSM3_Tweaks.ini' },
    @{ dir = $Shin4; dll = 'build\dinput8.dll'; ini = 'SSM4_Tweaks.ini' },
    @{ dir = $Dynasty8; dll = 'build\dinput8-platform.dll'; ini = 'SSM7_Tweaks.ini' }
)
Write-Warning "Skipped Sengoku Musou 2 ($Sengoku2): it still crashes with no patch DLL on this Windows 11 system, and a local dinput8.dll proxy adds a separate conflict. No compatibility-mode fix was confirmed; see SW2_DIAGNOSIS.md."
foreach ($item in $targets) {
    $target = (Resolve-Path -LiteralPath $item.dir).Path
    $backup = Join-Path $target ('OrochiZPadX-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    $manifest = @()
    foreach ($name in @('dinput8.dll', $item.ini, 'OrochiZPadX.ini', 'OrochiZPadX.log')) {
        $destination = Join-Path $target $name
        $existed = Test-Path -LiteralPath $destination
        if ($existed) { Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $name) -Force }
        $manifest += [pscustomobject]@{ name = $name; existed = $existed }
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
    Copy-Item -LiteralPath (Join-Path $root $item.dll) -Destination (Join-Path $target 'dinput8.dll') -Force
    $newConfig = Join-Path $target $item.ini
    $oldConfig = Join-Path $target 'OrochiZPadX.ini'
    if (-not (Test-Path -LiteralPath $newConfig)) {
        if (Test-Path -LiteralPath $oldConfig) { Copy-Item -LiteralPath $oldConfig -Destination $newConfig }
        else { Copy-Item -LiteralPath (Join-Path $root $item.ini) -Destination $newConfig }
    }
    if (Test-Path -LiteralPath $oldConfig) { Remove-Item -LiteralPath $oldConfig }
    Write-Output "Installed: $target"
    Write-Output "Backup: $backup"
}
