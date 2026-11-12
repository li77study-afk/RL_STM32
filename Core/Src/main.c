#include "main.h"
#include "q_app.h"
#include "lcd_spi_200.h"
#include <string.h>
void SystemClock_Config(void);
void MX_SPI6_Init(void);

int main(void)
{
    memset((void *)&g_diag, 0, sizeof(g_diag));
    SCB_EnableICache();
    SCB_EnableDCache();
    HAL_Init();
    SystemClock_Config();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_diag.stage = 1;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13; gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL; gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    MX_SPI6_Init();
    g_diag.stage = 2;
    SPI_LCD_Init();
    g_diag.stage = 3;
    Q_App_Init();
    while (1) Q_App_Poll();
}

void Error_Handler(void)
{
    g_diag.fatal_count++;
    __disable_irq();
    while (1) { }
}
