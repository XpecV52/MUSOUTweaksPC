param([string]$GameDir = 'F:\SteamLibrary\steamapps\common\Dynasty Warriors 7 DX')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$target = (Resolve-Path -LiteralPath $GameDir).Path
$executables = @('SM6TC.exe','SM6EN.exe','SM6JP.exe')
if (-not ($executables | Where-Object { Test-Path -LiteralPath (Join-Path $target $_) })) {
    throw 'No supported SSM6 DX executable (SM6TC.exe, SM6EN.exe, or SM6JP.exe) was found.'
}
$hasTraditionalChinese = Test-Path -LiteralPath (Join-Path $target 'SM6TC.exe')
$running = Get-Process -Name @('SM6TC','SM6EN','SM6JP') -ErrorAction SilentlyContinue
if ($running) { throw "Exit the SSM6 DX game before installing: $($running.ProcessName -join ', ')" }
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
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\Fix_for_SM6\enable.txt'; name='mods\Fix_for_SM6\enable.txt' },
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\resolution_for_SM6TC.dll'; name='mods\resolution_for_SM6TC.dll' },
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\resolution_for_SM6TC\config.ini'; name='mods\resolution_for_SM6TC\config.ini' },
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\resolution_for_SM6TC\enable.txt'; name='mods\resolution_for_SM6TC\enable.txt' },
    [pscustomobject]@{ source='supplemental\ssm6tc\mods\resolution_for_SM6TC\load.txt'; name='mods\resolution_for_SM6TC\load.txt' }
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
Write-Output "Installed in: $target"
Write-Output "Backup: $backup"
Write-Output 'Launch the game normally. The patch supports XInput, native DS4, D-pad mapping, hot-plug, and borderless desktop-resolution rendering.'
if ($hasTraditionalChinese) { Write-Output 'Installed the supplied SM6TC resolution/fix mods through the D3DX9 mod loader.' }
else { Write-Output 'SM6TC supplemental mods were skipped because SM6TC.exe is not present.' }
