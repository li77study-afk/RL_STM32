# 完整流程：构建 → 备份现有 128 KiB Flash → 校验备份长度 → 烧录 → 校验 → 复位。
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) {
    & "$PSScriptRoot/build.ps1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
# OpenOCD 使用 Tcl 风格路径，转换分隔符并在命令中用大括号保护路径。
$elf = (Join-Path $root 'build/tank_battle.elf').Replace('\','/')
if (-not (Test-Path -LiteralPath $elf)) { throw 'Build tank_battle.elf first.' }
New-Item -ItemType Directory -Force "$root/logs" | Out-Null
# 每次使用新时间戳保留旧备份；logs 目录不进入 Git。
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$backup = (Join-Path $root "logs/before_flash_$stamp.bin").Replace('\','/')
$ocd = 'D:/STM32Tools/openocd/bin/openocd.exe'
$common = @('-s','D:/STM32Tools/openocd/share/openocd/scripts','-f','interface/stlink.cfg','-f','target/stm32h7x.cfg','-c','adapter speed 4000')
# 0x08000000 为片内 Flash 起始地址；0x20000=131072 字节。
& $ocd @common -c 'init' -c 'halt' -c "dump_image {$backup} 0x08000000 0x20000" -c 'resume' -c 'shutdown' 2>&1 | Tee-Object "$root/logs/backup_$stamp.log"
if ($LASTEXITCODE -ne 0) { throw 'Flash backup failed; programming was not attempted.' }
if ((Get-Item -LiteralPath $backup).Length -ne 131072) { throw 'Flash backup length is incorrect.' }
Get-FileHash -Algorithm SHA256 -LiteralPath $backup | Format-List | Out-File "$root/logs/backup_$stamp.sha256.txt"
# verify 读回比较，reset 让新固件从复位入口运行，exit 释放 ST-LINK。
& $ocd @common -c "program {$elf} verify reset exit" 2>&1 | Tee-Object "$root/logs/flash_$stamp.log"
if ($LASTEXITCODE -ne 0) { throw 'Programming or verification failed.' }
Write-Host "Verified and running. Previous 128 KiB firmware saved to $backup"
