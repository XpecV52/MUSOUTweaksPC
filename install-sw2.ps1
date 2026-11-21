# 战国2安装脚本。自动替换旧 DLL 并保留可恢复的时间戳备份。
# SW2 installer. Replaces the old DLL and keeps a timestamped rollback backup.
param([string]$GameDir = 'E:\Games\Non-Steam\Sengoku Musou 2')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$target = (Resolve-Path -LiteralPath $GameDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $target 'SW2.exe'))) { throw '目标目录找不到 SW2.exe / SW2.exe was not found in the target directory.' }
$running = Get-Process -Name SW2 -ErrorAction SilentlyContinue
if ($running) {
    $localProxyLoaded = $false
    foreach ($process in $running) {
        try {
            if ($process.Modules | Where-Object { $_.ModuleName -ieq 'dinput8.dll' -and $_.FileName -ieq (Join-Path $target 'dinput8.dll') }) { $localProxyLoaded = $true }
        } catch { throw 'Could not verify the loaded dinput8.dll path. Exit SW2 before installing.' }
    }
    if ($localProxyLoaded) { throw 'The SW2 patch DLL is already loaded. Exit SW2 before replacing it.' }
    Write-Warning 'SW2 is running with the system dinput8.dll. Installation is safe, but restart the game to load this patch.'
}
$files = @('dinput8.dll','SM2_Tweaks.ini')
$backupFiles = @($files + 'SSM2_Tweaks.ini' + 'MUSOUTweaksPC.log')
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
    foreach ($name in $files) {
        $source = if ($name -eq 'dinput8.dll') { Join-Path $root 'build\dinput8.dll' } else { Join-Path $root $name }
        Copy-Item -LiteralPath $source -Destination (Join-Path $target $name) -Force
    }
    $legacyConfig = Join-Path $target 'SSM2_Tweaks.ini'
    if (Test-Path -LiteralPath $legacyConfig) { Remove-Item -LiteralPath $legacyConfig }
} catch {
    foreach ($entry in $manifest) {
        $destination = Join-Path $target $entry.name
        if ($entry.existed) { Copy-Item -LiteralPath (Join-Path $backup $entry.name) -Destination $destination -Force }
        elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
    }
    throw
}
Write-Output "已安装 / Installed in: $target"
Write-Output "备份 / Backup: $backup"
Write-Output '已保留原有 dinput8.dll.2.5-ime-only 文件。 / The existing dinput8.dll.2.5-ime-only file was left untouched.'
