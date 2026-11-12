# STM32 强化学习与嵌入式视觉实验

面向 **反客 FK750M6-XBH6 / STM32H750XBH6** 的两个独立 GCC/CMake 工程。

| 工程 | 内容 | 操作入口 |
|---|---|---|
| [Q-learning](projects/q_learning/README.md) | 板上从零训练 16×16 迷宫，100 张地图，详细中文注释 | `projects/q_learning` |
| [CNN 手写数字识别](projects/digit_cnn/README.md) | OV5640 实时预览、单数字预处理、纯 C FP32 CNN 推理 | `projects/digit_cnn` |

```text
projects/
├── q_learning/       原强化学习工程，完整保留
└── digit_cnn/        独立摄像头 CNN 工程
```

在 VS Code 中直接打开对应工程目录，可分别构建、烧录、调试。两个工程使用同一块板，烧录其中一个会替换板上固件；脚本都会先备份现有 128 KiB Flash。

本机工具链位于 `D:/STM32Tools`。例如从仓库根目录构建 CNN：

```powershell
pwsh -NoProfile -File projects/digit_cnn/tools/build.ps1
pwsh -NoProfile -File projects/digit_cnn/tools/flash.ps1
```

原强化学习代码已从仓库根目录移入 `projects/q_learning`，地图初始化入口仍为该工程 `Core/Src/q_app.c` 中的 `Map_Init(1)`。训练集、虚拟环境、构建产物和原固件备份只保存在本机，不提交 Git。
