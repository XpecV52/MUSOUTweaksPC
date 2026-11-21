# 批量安装当前构建到本机已配置的游戏目录，并为每个目标创建时间戳备份。
# Install the current build into the configured game folders and create a timestamped backup for each target.
param(
    [string]$Shin3 = 'E:\Games\Non-Steam\Shin Sangokumusou 3\game',
    [string]$Shin4 = 'E:\Games\Non-Steam\Shin Sangokumusou 4',
    [string]$Shin5 = 'E:\Games\Non-Steam\Shin Sangokumusou 5',
    [string]$Sengoku2 = 'E:\Games\Non-Steam\Sengoku Musou 2',
    [string]$Dynasty8 = 'F:\SteamLibrary\steamapps\common\Dynasty Warriors 8'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$patchedGames = @('ShinSangokumusou3','SM4_WIN','SS4','SS4_Plus','SS4_Super','SS4_Ultima','Launch','SW2','SM5_WIN')
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in $patchedGames }
if ($running) { throw "请先退出正在运行的游戏 / Exit patched games before installing: $($running.ProcessName -join ', ')" }
$targets = @(
    @{ dir = $Shin3; dll = 'build\dinput8.dll'; ini = 'SSM3_Tweaks.ini' },
    @{ dir = $Shin4; dll = 'build\dinput8.dll'; ini = 'SSM4_Tweaks.ini' },
    @{ dir = $Shin5; dll = 'build\dinput8.dll'; ini = 'SSM5_Tweaks.ini' },
    @{ dir = $Sengoku2; dll = 'build\dinput8.dll'; ini = 'SM2_Tweaks.ini' },
    @{ dir = $Dynasty8; dll = 'build\dinput8-platform.dll'; ini = 'SSM7_Tweaks.ini' }
)
foreach ($item in $targets) {
    $target = (Resolve-Path -LiteralPath $item.dir).Path
    $backup = Join-Path $target ('MUSOUTweaksPC-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    $manifest = @()
    $backupNames = @('dinput8.dll', $item.ini, 'OrochiZPadX.ini', 'OrochiZPadX.log', 'MUSOUTweaksPC.log')
    if ($item.ini -eq 'SM2_Tweaks.ini') { $backupNames += 'SSM2_Tweaks.ini' }
    foreach ($name in $backupNames) {
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
        if ($item.ini -ne 'SM2_Tweaks.ini' -and (Test-Path -LiteralPath $oldConfig)) { Copy-Item -LiteralPath $oldConfig -Destination $newConfig }
        else { Copy-Item -LiteralPath (Join-Path $root $item.ini) -Destination $newConfig }
    }
    if ($item.ini -ne 'SM2_Tweaks.ini' -and (Test-Path -LiteralPath $oldConfig)) { Remove-Item -LiteralPath $oldConfig }
    if ($item.ini -eq 'SM2_Tweaks.ini') {
        $legacyConfig = Join-Path $target 'SSM2_Tweaks.ini'
        if (Test-Path -LiteralPath $legacyConfig) { Remove-Item -LiteralPath $legacyConfig }
    }
    Write-Output "已安装 / Installed: $target"
    Write-Output "备份 / Backup: $backup"
}
