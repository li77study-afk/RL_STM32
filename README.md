# RL_STM32：在 STM32H750 上从零学习 Q-learning

使用 **反客 FK750M6-XBH6 / STM32H750XBH6 + 2.00 寸 ST7789V2 SPI 屏**，在单片机上训练一个 16×16 迷宫智能体。算法是纯 C 表格 Q-learning，训练和推理都在板上完成。

上电后 Q 表清零，先展示探索，再加速训练；完成 5000 回合后关闭探索，循环演示学到的路线。按 RESET 键重新从零训练。Q 表占 **4096 字节**，帧缓冲放在片内 AXI SRAM，运行不需要摄像头或外部 SDRAM。

包含 **100 张不同地图**，已通过全路格连通、去重和 300 次多种子学习测试。实机结果见 [验证记录和画面](docs/BOARD_VALIDATION.md)。

## 换地图：改初始化函数的一行

打开 [Core/Src/q_app.c](Core/Src/q_app.c)，在 `Q_App_Init()` 中找到：

```c
if (!Map_Init(1)) Error_Handler();
```

将 `1` 改成 `1～100` 中的编号，例如 `Map_Init(42)`，再执行 `tools/flash.ps1`（自动编译并烧录）。`Map_Init()` 把选定地图复制到当前地图，随后 `ql_init()` 清空 Q 表，开始学习新环境。无需切图命令。

- [离线图册 maps/gallery.html](maps/gallery.html)：下载/本地用浏览器打开，可按编号、类别筛选，点击卡片显示初始化调用。
- [100 张地图编号与文字预览](maps/README.md)：在 GitHub 上可直接阅读。
- [机器可读地图库](maps/catalog.json)：每张图包含格子、种子、最短距离等信息。

| 编号 | 地图结构 |
|---|---|
| 001～025 | 开放区域与随机障碍 |
| 026～050 | 带缺口的交错围栏 |
| 051～075 | 有分支和回路的迷宫 |
| 076～100 | 绕行更长的通道迷宫 |

每张图都是 16×16，左上角起点、右下角终点，所有道路格子相互连通。可通过 `python tools/generate_maps.py` 按固定种子重建地图库及图册。

## 屏幕说明

| 显示 | 含义 |
|---|---|
| MAP | 当前地图编号 / 100 |
| TRAIN / DEMO | 正在训练 / 使用已学策略演示 |
| EP | 已完成回合数 / 5000 |
| EPS | 随机探索概率，从 100% 逐渐降到 5% |
| WIN | 最近至多 100 个训练回合到达终点的比例 |
| 黄色方块 | 当前智能体位置 |
| S / G | 起点 / 终点，灰色为墙 |
| 小箭头 | 当前 Q 表中该位置的贪心动作 |
| STEP | 当前回合步数；奖励和可在调试器的 `g_agent.episode_return` 中查看 |
| PATH / BEST | 关闭探索后实际走出的路径长度 / 该图的参考最短步数 |

地图库的最短路径范围为 30～92 步，BFS 只作测试对照，不参与固件训练。未学出路线时显示 `NO ROUTE`，不会播放预先写好的答案。屏幕上的 `BEST` 是该图的参考最短步数，`PATH` 是模型实际学到的步数。

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

1. [Learning/map_library.c](Learning/map_library.c)：`Map_Init()` 加载地图；[地图数据](Learning/map_data.inc)包含全部 100 张图。
2. [Learning/q_learning.h](Learning/q_learning.h)：状态、动作、奖励和超参数。
3. [Learning/q_learning.c](Learning/q_learning.c)：环境转移、ε-greedy、TD 更新、回合结束。
4. [Core/Src/q_app.c](Core/Src/q_app.c)：训练节拍、成功率、演示和 SWD 诊断。
5. [Core/Src/q_display.c](Core/Src/q_display.c)：RGB565 迷宫画面。
6. [docs/LEARNING_GUIDE.md](docs/LEARNING_GUIDE.md)：公式与建议实验。

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
cc -std=c11 -O2 -Wall -Wextra -Werror -ILearning Learning/q_learning.c Learning/map_library.c tests/test_q_learning.c -lm -o test_q_learning
./test_q_learning
```

覆盖边界、撞墙、终点不 bootstrap、TD 数值、回合截断、评估不修改模型，以及 100 张图 × 3 个种子（300 次训练）与独立最短路的对照。详见 [第三方代码来源](THIRD_PARTY.md)。
