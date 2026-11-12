param([string]$Compiler = 'D:/GOWIN/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force "$root/build-host" | Out-Null
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Pass -Compiler with a native C compiler path.' }
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror "-I$root/Learning" "$root/Learning/q_learning.c" "$root/tests/test_q_learning.c" -o "$root/build-host/test_q_learning.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$root/build-host/test_q_learning.exe"
exit $LASTEXITCODE
