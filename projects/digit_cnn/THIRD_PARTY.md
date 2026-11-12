# 第三方代码和数据来源

- `Drivers/CMSIS` 为 Arm/ST 的核心和设备头文件；`Drivers/STM32H7xx_HAL_Driver`、启动文件、系统文件来自 STMicroelectronics 配套代码，保留原始版权和许可说明。
- `Drivers/User` 的 OV5640、SCCB、SPI LCD 和字库来自反客 FK750M6-XBH6 例程及本机已验证的摄像头工程修改，保留原作者注释；不将这些代码重新声明为项目原创。
- 摄像头双缓冲、SDRAM 和 SPI 配置复用本机 `STM32H750XBH6/projects/camera_spi` 的已工作实现。本工程在其上增加 CNN、图像预处理与调度。
- [MNIST](https://yann.lecun.org/exdb/mnist/) 由 Yann LeCun、Corinna Cortes、Christopher J. C. Burges 提供；项目从 [TensorFlow/Keras 数据集托管地址](https://storage.googleapis.com/tensorflow/tf-keras-datasets/mnist.npz) 下载并验证 SHA-256。完整数据集不随仓库提交，仅嵌入少量开机测试样本用于验证。
- 训练使用 [PyTorch](https://pytorch.org/)；固件不依赖 PyTorch 或其运行时。

模型权重为本次固定种子训练生成，训练配置、测试划分和结果都保存在 `models/training_report.json`。
