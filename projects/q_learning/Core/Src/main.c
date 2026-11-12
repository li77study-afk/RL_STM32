/**
 * 程序入口：供电/时钟 → GPIO/SPI → 屏幕 → 选图/学习器 → 持续轮询。
 * 复位后 startup_stm32h750xx.s 已设置栈、复制 .data、清零普通 .bss，
 * 并完成 SystemInit；这里继续初始化应用所需硬件。
 */
#include "main.h"
#include "q_app.h"
#include "lcd_spi_200.h"
#include <string.h>
void SystemClock_Config(void);
void MX_SPI6_Init(void);

int main(void)
{
    /* .diagnostics 为 NOLOAD 区，复位后可能保留旧值，需要手动清零。 */
    memset((void *)&g_diag, 0, sizeof(g_diag));
    /* I-Cache 加速取指，D-Cache 加速普通 SRAM 数据访问；DTCM 本身不走 Cache。 */
    SCB_EnableICache();
    SCB_EnableDCache();
    HAL_Init();  /* 配置 HAL 基础环境和 SysTick 毫秒时基。 */
    SystemClock_Config();
    /* DWT 周期计数器用于测量算法和画图开销；仅依赖内核，无需外接仪器。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_diag.stage = 1;
    /* PC13 是核心板指示灯。先开 GPIO 时钟，再配置引脚。 */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13; gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL; gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    MX_SPI6_Init(); /* SPI 外设初始化会回调 board.c 的 HAL_SPI_MspInit 配置引脚。 */
    g_diag.stage = 2;
    SPI_LCD_Init();
    g_diag.stage = 3;
    Q_App_Init(); /* 其中 Map_Init(1) 是你修改地图编号的位置。 */
    while (1) Q_App_Poll();
}

void Error_Handler(void)
{
    /* 保留错误计数供 ST-LINK 检查，然后停在这里，防止错误状态下继续运行。 */
    g_diag.fatal_count++;
    __disable_irq();
    while (1) { }
}
