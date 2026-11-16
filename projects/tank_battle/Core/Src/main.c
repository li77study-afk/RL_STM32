/** M6 启动：物理 SDRAM 自检 → 打开其 Cache → 从零训练坦克策略。 */
#include "main.h"
#include "tank_app.h"
#include "lcd_spi_200.h"
#include <string.h>
void SystemClock_Config(void);
void MX_SPI6_Init(void);

static void sdram_mpu(int cached)
{
    MPU_Region_InitTypeDef region={0};HAL_MPU_Disable();
    region.Enable=MPU_REGION_ENABLE;region.Number=MPU_REGION_NUMBER0;
    region.BaseAddress=0xc0000000;region.Size=MPU_REGION_SIZE_16MB;
    region.AccessPermission=MPU_REGION_FULL_ACCESS;region.DisableExec=MPU_INSTRUCTION_ACCESS_DISABLE;
    region.TypeExtField=MPU_TEX_LEVEL1;region.IsShareable=MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable=cached?MPU_ACCESS_CACHEABLE:MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable=cached?MPU_ACCESS_BUFFERABLE:MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

int main(void)
{
    memset((void *)&g_diag,0,sizeof(g_diag));
    SCB_EnableICache();sdram_mpu(0);HAL_Init();SystemClock_Config();
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    g_diag.stage=1;
    __HAL_RCC_GPIOC_CLK_ENABLE();GPIO_InitTypeDef led={0};
    led.Pin=GPIO_PIN_13;led.Mode=GPIO_MODE_OUTPUT_PP;led.Pull=GPIO_NOPULL;led.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC,&led);MX_SPI6_Init();SPI_LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);LCD_SetBackColor(LCD_BLACK);LCD_SetColor(LCD_WHITE);LCD_Clear();
    LCD_DisplayString(0,16,"TANK BATTLE START");LCD_DisplayString(0,48,"CHECK SDRAM 16 MiB");
    g_diag.stage=2;Board_SDRAM_Init();
    /* 自检期间不缓存 SDRAM，读写会真正到芯片。通过后 Q 表仅由 CPU 访问，
     * 可以改为写回缓存；外部存储器没有 DMA 并发访问。 */
    sdram_mpu(1);SCB_EnableDCache();g_diag.stage=3;
    Tank_App_Init();while(1)Tank_App_Poll();
}

void Error_Handler(void)
{
    g_diag.fatal_count++;__disable_irq();while(1){}
}
