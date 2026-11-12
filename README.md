# RL_STM32：在 STM32H750 上从零学习 Q-learning

使用 **反客 FK750M6-XBH6 / STM32H750XBH6 + 2.00 寸 ST7789V2 SPI 屏**，在单片机上训练一个 8×8 迷宫智能体。算法是纯 C 表格 Q-learning，训练和推理都在板上完成。

上电后 Q 表清零，先展示探索，再加速训练；完成 1500 回合后关闭探索，循环演示学到的路线。按 RESET 键重新从零训练。Q 表占 **1024 字节**，帧缓冲放在片内 AXI SRAM，运行不需要摄像头或外部 SDRAM。

已实机验证：约 53 秒完成训练，学到 20 步最短路线。详见 [验证记录和画面](docs/BOARD_VALIDATION.md)。

## 屏幕说明

| 显示 | 含义 |
|---|---|
| TRAIN / DEMO | 正在训练 / 使用已学策略演示 |
| EP | 已完成回合数 / 1500 |
| EPS | 随机探索概率，从 100% 逐渐降到 5% |
| WIN | 最近至多 100 个训练回合到达终点的比例 |
| 黄色方块 | 当前智能体位置 |
| S / G | 起点 / 终点，灰色为墙 |
| 小箭头 | 当前 Q 表中该位置的贪心动作 |
| STEP / RETURN | 当前回合步数 / 累积奖励 |
| GREEDY PATH | 关闭探索后得到的路径长度 |

默认地图最短路径为 20 步，由测试中的独立 BFS 对照得到。BFS 不参与固件训练。未学出路线时显示 `NO ROUTE`，不会播放预先写好的答案。

## 硬件连接

| 信号 | 引脚 |
|---|---|
| LCD SCK / MOSI | PG13 / PG14 |
| LCD CS / DC / 背光 | PA15 / PG12 / PH6 |
| 用户 LED（低电平亮） | PC13 |
| ST-LINK SWDIO / SWCLK | PA13 / PA14 |
| ST-LINK 参考电压 / 地 | 目标板 3.3 V / GND |

沿用现有 M6 + SPI200M1-240×320 屏幕接线。时钟采用 25 MHz HSE、LDO 供电、480 MHz CPU；SPI6 为 60 MHz。换板时需核对时钟、供电和引脚。

## 编译、烧录、监测

本机依赖位于 `D:/STM32Tools`：Arm GNU Toolchain 13.3.1、CMake 4.4.3、Ninja、OpenOCD 0.12.0。Python 用于读取 SWD 数据。VS Code 打开本目录即可使用 `build` 任务和 `Q-learning ST-Link` 调试配置。

```powershell
pwsh -NoProfile -File tools/build.ps1
pwsh -NoProfile -File tools/flash.ps1
python tools/monitor.py --seconds 10
```

烧录脚本先备份整个 128 KiB 片内 Flash 到带时间戳的 `logs/before_flash_*.bin`，成功后才烧录并读回校验。产物为 `build/q_learning.elf`、`.hex`、`.bin`、`.map`。同一个 ST-LINK 不应同时被 IDE 和监测程序占用。

清空 Q 表并记录完整训练：

```powershell
python tools/monitor.py --restart --seconds 75 --output logs/training.json
```

监测程序自动启动和关闭自己的 OpenOCD，不暂停 CPU，不需要串口线。`logs/` 内的原固件备份和本地日志不上传 GitHub；公开验证摘要位于 `docs/`。

## 代码阅读顺序

1. [Learning/q_learning.h](Learning/q_learning.h)：状态、动作、奖励和超参数。
2. [Learning/q_learning.c](Learning/q_learning.c)：环境转移、ε-greedy、TD 更新、回合结束。
3. [Core/Src/q_app.c](Core/Src/q_app.c)：训练节拍、成功率、演示和 SWD 诊断。
4. [Core/Src/q_display.c](Core/Src/q_display.c)：RGB565 迷宫画面。
5. [docs/LEARNING_GUIDE.md](docs/LEARNING_GUIDE.md)：公式与建议实验。

核心更新：

```c
float future = t.terminal ? 0.0f : a->q[t.next_state][ql_greedy_action(a, t.next_state)];
float old_q = a->q[s][action];
float target = (float)t.reward + QL_GAMMA * future;
float td_error = target - old_q;
a->q[s][action] = old_q + QL_ALPHA * td_error;
```

## 算法测试

本机使用已有的 LLVM MinGW 编译器，也可用 `-Compiler` 指定原生 C 编译器：

```powershell
pwsh -NoProfile -File tools/test.ps1
```

Linux/macOS：

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -ILearning Learning/q_learning.c tests/test_q_learning.c -lm -o test_q_learning
./test_q_learning
```

覆盖边界、撞墙、终点不 bootstrap、TD 数值、回合截断、评估不修改模型，以及 33 个种子与独立最短路的对照。详见 [第三方代码来源](THIRD_PARTY.md)。
