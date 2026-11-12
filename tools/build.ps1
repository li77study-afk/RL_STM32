# 以脚本所在目录定位工程，允许从任意工作目录调用。
# RelWithDebInfo 同时保留 -O2 优化和调试符号，可用 nm/ST-LINK 查询变量。
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
$cmakeExe = 'D:\STM32Tools\cmake-4.4.3-windows-x86_64\bin\cmake.exe'
& $cmakeExe -S $projectDir -B "$projectDir/build" -G Ninja -DCMAKE_MAKE_PROGRAM=D:/STM32Tools/ninja/ninja.exe -DCMAKE_BUILD_TYPE=RelWithDebInfo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 交给 Ninja 增量编译；-j 8 是最多 8 个编译进程，不是多个固件任务。
& $cmakeExe --build "$projectDir/build" -j 8
exit $LASTEXITCODE
