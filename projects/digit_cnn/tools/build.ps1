$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
$cmakeExe = 'D:\STM32Tools\cmake-4.4.3-windows-x86_64\bin\cmake.exe'
& $cmakeExe -S $projectDir -B "$projectDir/build" -G Ninja -DCMAKE_MAKE_PROGRAM=D:/STM32Tools/ninja/ninja.exe -DCMAKE_BUILD_TYPE=RelWithDebInfo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmakeExe --build "$projectDir/build" -j 8
exit $LASTEXITCODE
