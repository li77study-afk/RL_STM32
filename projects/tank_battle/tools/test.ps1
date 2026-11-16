param([string]$Compiler = 'D:/GOWIN/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force "$root/build-host" | Out-Null
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror "-I$root/Game" "-I$root/Learning" "$root/Game/battle.c" "$root/Game/fixed_policy.c" "$root/Learning/tank_agent.c" "$root/Game/arena_maps.c" "$root/tests/test_battle.c" -o "$root/build-host/test_battle.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root/build-host/test_battle.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 使用测试替身检查阶段路由，故意不链接生产 fixed_policy.c。
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror "-I$root/Game" "-I$root/Learning" "$root/Game/battle.c" "$root/Game/arena_maps.c" "$root/Learning/tank_agent.c" "$root/tests/test_policy_routing.c" -o "$root/build-host/test_policy_routing.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root/build-host/test_policy_routing.exe"
exit $LASTEXITCODE
