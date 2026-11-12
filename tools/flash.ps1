param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) {
    & "$PSScriptRoot/build.ps1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$elf = (Join-Path $root 'build/q_learning.elf').Replace('\','/')
if (-not (Test-Path -LiteralPath $elf)) { throw 'Build q_learning.elf first.' }
New-Item -ItemType Directory -Force "$root/logs" | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$backup = (Join-Path $root "logs/before_flash_$stamp.bin").Replace('\','/')
$ocd = 'D:/STM32Tools/openocd/bin/openocd.exe'
$common = @('-s','D:/STM32Tools/openocd/share/openocd/scripts','-f','interface/stlink.cfg','-f','target/stm32h7x.cfg','-c','adapter speed 4000')
& $ocd @common -c 'init' -c 'halt' -c "dump_image {$backup} 0x08000000 0x20000" -c 'resume' -c 'shutdown' 2>&1 | Tee-Object "$root/logs/backup_$stamp.log"
if ($LASTEXITCODE -ne 0) { throw 'Flash backup failed; programming was not attempted.' }
if ((Get-Item -LiteralPath $backup).Length -ne 131072) { throw 'Flash backup length is incorrect.' }
Get-FileHash -Algorithm SHA256 -LiteralPath $backup | Format-List | Out-File "$root/logs/backup_$stamp.sha256.txt"
& $ocd @common -c "program {$elf} verify reset exit" 2>&1 | Tee-Object "$root/logs/flash_$stamp.log"
if ($LASTEXITCODE -ne 0) { throw 'Programming or verification failed.' }
Write-Host "Verified and running. Previous 128 KiB firmware saved to $backup"
