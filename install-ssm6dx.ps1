# 真三6DX专用安装脚本。繁中版会同时安装随包提供的面部表情 MOD。
# SSM6 DX installer. The Traditional-Chinese build also installs the bundled facial-expression mod.
param([string]$GameDir = 'F:\SteamLibrary\steamapps\common\Dynasty Warriors 7 DX')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$target = (Resolve-Path -LiteralPath $GameDir).Path
$executables = @('SM6TC.exe','SM6EN.exe','SM6JP.exe')
if (-not ($executables | Where-Object { Test-Path -LiteralPath (Join-Path $target $_) })) {
    throw '找不到支持的 SSM6 DX 可执行文件 / No supported SSM6 DX executable (SM6TC.exe, SM6EN.exe, or SM6JP.exe) was found.'
}
$hasTraditionalChinese = Test-Path -LiteralPath (Join-Path $target 'SM6TC.exe')
$running = Get-Process -Name @('SM6TC','SM6EN','SM6JP') -ErrorAction SilentlyContinue
if ($running) { throw "请先退出真三6DX / Exit the SSM6 DX game before installing: $($running.ProcessName -join ', ')" }
$installFiles = @(
    [pscustomobject]@{ source='build\dinput8.dll'; name='dinput8.dll' }
)
$configPath = Join-Path $target 'SSM6_Tweaks.ini'
if (-not (Test-Path -LiteralPath $configPath)) {
    $installFiles += [pscustomobject]@{ source='SSM6_Tweaks.ini'; name='SSM6_Tweaks.ini' }
}
$supplementalFiles = @(
    [pscustomobject]@{ source='supplemental\ssm6tc\d3dx9_43.dll'; name='d3dx9_43.dll' },
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\Fix_for_SM6.dll'; name='mods\Fix_for_SM6.dll' },
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\Fix_for_SM6\enable.txt'; name='mods\Fix_for_SM6\enable.txt' }
)
if ($hasTraditionalChinese) { $installFiles += $supplementalFiles }
foreach ($item in $installFiles) {
    $source = Join-Path $root $item.source
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing patch file: $source" }
}
$backupFiles = @('dinput8.dll','SSM6_Tweaks.ini','OrochiZPadX.ini','MUSOUTweaksPC.log')
if ($hasTraditionalChinese) { $backupFiles += $supplementalFiles | ForEach-Object { $_.name } }
$backup = Join-Path $target ('MUSOUTweaksPC-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$manifest = @()
foreach ($name in $backupFiles) {
    $destination = Join-Path $target $name
    $existed = Test-Path -LiteralPath $destination
    if ($existed) {
        $backupPath = Join-Path $backup $name
        $backupParent = Split-Path -Parent $backupPath
        if (-not (Test-Path -LiteralPath $backupParent)) { New-Item -ItemType Directory -Path $backupParent -Force | Out-Null }
        Copy-Item -LiteralPath $destination -Destination $backupPath -Force
    }
    $manifest += [pscustomobject]@{name=$name; existed=$existed}
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8
try {
    foreach ($item in $installFiles) {
        $destination = Join-Path $target $item.name
        $destinationParent = Split-Path -Parent $destination
        if (-not (Test-Path -LiteralPath $destinationParent)) { New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null }
        Copy-Item -LiteralPath (Join-Path $root $item.source) -Destination $destination -Force
    }
    $legacyConfig = Join-Path $target 'OrochiZPadX.ini'
    if (Test-Path -LiteralPath $legacyConfig) { Remove-Item -LiteralPath $legacyConfig }
} catch {
    foreach ($entry in $manifest) {
        $destination = Join-Path $target $entry.name
        if ($entry.existed) {
            $destinationParent = Split-Path -Parent $destination
            if (-not (Test-Path -LiteralPath $destinationParent)) { New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null }
            Copy-Item -LiteralPath (Join-Path $backup $entry.name) -Destination $destination -Force
        }
        elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
    }
    throw
}
Write-Output "已安装 / Installed in: $target"
Write-Output "备份 / Backup: $backup"
Write-Output '请正常启动游戏。补丁支持 XInput、原生 DS4、十字键、热插拔和无边框桌面分辨率渲染。 / Launch the game normally. The patch supports XInput, native DS4, D-pad mapping, hot-plug, and borderless desktop-resolution rendering.'
if ($hasTraditionalChinese) { Write-Output '已通过 D3DX9 MOD 加载器安装真三6DX繁中版面部表情修复。未安装分辨率 MOD，因为它会让引擎渲染尺寸与逻辑分辨率不一致。 / Installed the supplied SM6TC facial-expression fix. The resolution mod is intentionally omitted because it mismatches the engine render and logical resolutions.' }
else { Write-Output '未发现 SM6TC.exe，跳过繁中版第三方 MOD。 / SM6TC.exe is absent, so the Traditional-Chinese supplemental mods were skipped.' }
