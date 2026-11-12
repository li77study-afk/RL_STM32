# STM32H750 + OV5640 手写数字 CNN

独立摄像头工程：电脑训练一个小型 CNN，STM32H750 使用**手写纯 C FP32 算子**完成前向推理。配套 FK750M6-XBH6、OV5640 和 2.00 寸 240×320 SPI 屏。

当前版本支持取景框内**白纸上单个、清晰的黑色手写数字 0～9**。不包含整页文字识别、多个数字分割或通用物体检测。模型和算子已验证，真实纸面识别率还需要采集实拍样本评估。

## 已完成的验证

| 项目 | 结果 |
|---|---:|
| 参数量 | 9,098 |
| 计算量 | 290,080 MAC/次 |
| 独立 MNIST 测试集 | 9,863 / 10,000，98.63% |
| C 与 PyTorch 分类结果对照 | 10,000 / 10,000 一致 |
| 合成白纸黑字预处理 + CNN | 973 / 1,000，97.3%（不是实拍） |
| 板上开机模型自检 | 10 / 10 |
| 板上 100 张已知图像 | 100 / 100 与参考预测一致；99 / 100 标签正确 |
| 板上 FP32 推理 | 约 7.36 ms/次 |
| 摄像头预览 | 约 29～30 FPS |

详见 [验证记录](docs/VALIDATION.md)。98.63% 是标准数据集结果，不能直接解释为真实镜头拍摄的准确率。

## 使用方法

1. 连接 M6 核心板的 OV5640、SPI200M1 屏幕、ST-LINK，保持原有摄像头接线。
2. 在 VS Code 中直接打开本目录，执行 `flash ST-Link` 任务，或运行下方命令。
3. 上电自动执行 10 张数字样本自检，随后进入实时相机模式。
4. 用较粗黑笔在白纸上写一个独立数字，将它完整放进青色框内，留出边距，并确保光照和对焦清晰。
5. 右下角显示实际送入网络的 28×28 图像：应是黑底亮色数字，不应只是空白、整块阴影或纸边。

```powershell
pwsh -NoProfile -File tools/build.ps1
pwsh -NoProfile -File tools/flash.ps1
```

工具链沿用本机 `D:/STM32Tools` 下的 GCC 13.3.1、CMake、Ninja 和 OpenOCD。**构建和上板运行不需要安装 PyTorch**，训练好的 C 参数已经包含在仓库。

烧录前脚本先备份完整 128 KiB Flash 到本机 `logs/before_flash_*.bin`，再烧录并读回校验。产物为 `build/digit_cnn.elf/.bin/.hex/.map`。

## 屏幕内容

- `DIGIT`：通过前景检查、置信度和连续两次稳定预测后的结果；`--` 表示当前未确认。
- `CONF`：分类器的 Softmax 概率，不代表面对任意场景都可靠。
- `SELFTEST 10/10 LIVE`：开机样本自检结果和实时模式。
- 青/绿色框：取景范围；绿色表示已有确认结果。
- `CNN / PRE`：最近一次推理/预处理耗时；`LCD` 为实际预览帧率。
- `28x28 INPUT`：网络实际输入的放大预览。

预览最多约 30 FPS，摄像头预处理/识别最多约 10 Hz。相机持续采集，应用取最新完整帧，主动跳过过期帧。

## 模型和图像处理

```text
输入 1×28×28，uint8/255
→ Conv3×3，8 通道，same padding → ReLU → MaxPool2
→ Conv3×3，16 通道，same padding → ReLU → MaxPool2
→ 展平 784 → 全连接 10 → Softmax
```

```text
OV5640 → DCMI/DMA 双缓冲（SDRAM）
       → 稳定副本（AXI SRAM）
       → 中央 128×128 ROI → 灰度 → Otsu 定位最大深色连通域
       → 保持比例缩到 20×20 内容框 → 补到 28×28 → 灰度反色/重心对齐
       → CNN（DTCM 工作区）→ 拒识/稳定判断 → 屏幕
```

取景框的颜色在预处理之后才叠加，不会送入网络。预处理只针对白纸深色笔画；前景和置信度检查不能保证拒绝所有非数字物体。

## 阅读代码

| 文件 | 内容 |
|---|---|
| [ML/digit_cnn.c](ML/digit_cnn.c) | 手写卷积、ReLU、池化、全连接、稳定 Softmax；中文注释 |
| [ML/digit_preprocess.c](ML/digit_preprocess.c) | RGB565 转灰度、连通域、缩放、重心对齐 |
| [ML/digit_model.c](ML/digit_model.c) | 训练后自动导出的只读参数 |
| [Core/Src/vision_app.c](Core/Src/vision_app.c) | 模型自检、推理调度、拒识、显示和诊断 |
| [Core/Src/camera_app.c](Core/Src/camera_app.c) | 已验证的相机双缓冲，以及预览降帧调度 |
| [tools/train.py](tools/train.py) | 训练、验证集选检查点、最终测试和 C 导出 |
| [docs/LEARNING_GUIDE.md](docs/LEARNING_GUIDE.md) | 数组布局、计算量和学习实验 |

## 测试与调试

安装本机测试/截图所需库：

```powershell
python -m pip install -r requirements.txt
python tools/test_native.py
python tools/monitor.py --seconds 20
python tools/capture.py --prefix logs/my_digit
```

`test_native.py` 使用本机已有的 LLVM MinGW Clang 编译真实 C 算子为 DLL，测试 10,000 张 MNIST 和 1,000 张合成相机图。首次运行会下载并校验数据集。训练后本机若有 `models/reference.npz`，还会比较 C 与 PyTorch 的全部 logits。

`monitor.py` 只读 SWD 诊断，不暂停 CPU。`capture.py` 暂时冻结 CPU 预览副本，保存相机图、28×28 输入和诊断 JSON，然后恢复预览；相机 DMA 持续运行。

```powershell
python tools/board_test.py --count 100
```

板上测试通过 ST-LINK 直接发送已知 28×28 输入，屏幕显示 `TEST`，结束后恢复 `LIVE`。它绕过摄像头预处理，因此只验证实际 STM32 推理。运行这些工具时应关闭其它占用同一个 ST-LINK 的调试会话。

## 重新训练

已有 `.venv` 包含本次训练依赖。新机器可以创建环境，安装 CPU 版 PyTorch：

```powershell
python -m venv .venv --system-site-packages
& .venv/Scripts/python.exe -m pip install -r requirements.txt
& .venv/Scripts/python.exe -m pip install torch==2.15.0 --index-url https://download.pytorch.org/whl/cpu
& .venv/Scripts/python.exe tools/train.py --epochs 12 --threads 4
```

训练使用固定种子 750，将原始 60,000 张训练图划为 55,000 训练、5,000 验证；验证集选最佳检查点，最终再测独立 10,000 张测试图。增加小幅旋转、缩放和平移增强。训练报告位于 [models/training_report.json](models/training_report.json)。

训练会更新 `ML/digit_model.c`、`ML/digit_test_data.c`，然后需要重新运行原生测试、编译和烧录。数据集、模型检查点、参考大数组、虚拟环境和原固件备份不进入 Git。

当前未启用 OV5640 自动对焦固件，近距离拍摄要先确认清晰度。真实纸面样本、误识别案例和置信度阈值调整是下一阶段工作。第三方来源见 [THIRD_PARTY.md](THIRD_PARTY.md)。
