# 使用电脑本机的 C 编译器，不是 arm-none-eabi-gcc；生成的测试程序在 Windows 运行。
# 学习算法、地图库与板上使用完全相同的源码，测试额外加入 BFS 对照。
param([string]$Compiler = 'D:/GOWIN/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force "$root/build-host" | Out-Null
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Pass -Compiler with a native C compiler path.' }
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror "-I$root/Learning" "$root/Learning/q_learning.c" "$root/Learning/map_library.c" "$root/tests/test_q_learning.c" -o "$root/build-host/test_q_learning.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root/build-host/test_q_learning.exe"
exit $LASTEXITCODE
