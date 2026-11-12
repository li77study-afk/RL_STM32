# 第三方代码来源

- `Drivers/STM32H7xx_HAL_Driver`、ST 设备头文件、系统和启动文件、HAL 配置来自本机已工作的 FK750M6-XBH6 摄像头工程，保留 STMicroelectronics 原始版权和文件内许可声明。
- `Drivers/CMSIS/Include` 为 Arm CMSIS 核心头文件，保留原始版权与 Apache-2.0 等文件内许可说明。
- `Drivers/User` 的 LCD 驱动与字库来自反客科技 FK750M6-XBH6 / SPI200M1 例程及现有工程中的 SPI 发送修改，保留原作者说明。上游文件未提供统一的仓库级许可，不将这些文件重新许可为本项目原创代码。
- `Core/Src/board.c` 从已验证的板级工程提取时钟、供电与 SPI 配置。

`Learning/` 中的算法无板级依赖，可单独在电脑上编译和测试。
