param([Parameter(Mandatory=$true)][string]$BackupDir)
$ErrorActionPreference = 'Stop'
$patchedGames = @('OROCHI_Z_TC','ShinSangokumusou3','SM4_WIN','SS4','SS4_Plus','SS4_Super','SS4_Ultima','Launch')
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in $patchedGames }
if ($running) { throw "Exit patched games before restoring: $($running.ProcessName -join ', ')" }
$backup = (Resolve-Path -LiteralPath $BackupDir).Path
$target = Split-Path -Parent $backup
$manifestPath = Join-Path $backup 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'Backup directory must contain manifest.json.' }
$entries = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($entry in $entries) {
    if ($entry.name -notin @('dinput8.dll','OrochiZPadX.ini','SSM3_Tweaks.ini','SSM4_Tweaks.ini','SSM7_Tweaks.ini','OrochiZPadX.log','OrochiZPadX-diagnose.exe')) { throw 'Unrecognized manifest file.' }
    if ($entry.existed -and -not (Test-Path -LiteralPath (Join-Path $backup $entry.name))) { throw "Missing backup: $($entry.name)" }
}
foreach ($entry in $entries) {
    $destination = Join-Path $target $entry.name
    if ($entry.existed) { Copy-Item -LiteralPath (Join-Path $backup $entry.name) -Destination $destination -Force }
    elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
}
Write-Output "Restored from: $backup"
