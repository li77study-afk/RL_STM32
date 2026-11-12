/**
 ******************************************************************************
 * @file    exit_run0mode.c
 * @brief   复位后退出 Run mode0,使能对应供电模式 (由 GCC 启动文件调用)
 *
 * 说明: 新版 ST 官方 GCC 启动文件 (startup_stm32h750xx.s) 在复位后调用
 *       ExitRun0Mode(),Keil(ARMCC)启动文件不调用它。本工程供电为 LDO
 *       (宏 USE_PWR_LDO_SUPPLY),函数来自 ST 官方模板 system_stm32h7xx.c。
 ******************************************************************************
 */

#include "stm32h7xx_hal.h"

/**
 * @brief  退出 Run mode0,按供电方式配置 PWR->CR3 并等待电压稳定
 * @note   由 startup_stm32h750xx.s 的 Reset_Handler 调用
 * @param  无
 * @retval 无
 */
void ExitRun0Mode(void)
{
#if defined(USE_PWR_LDO_SUPPLY)
  #if defined(SMPS)
    /* 退出 Run* 模式: 关闭 SMPS,使能 LDO */
    PWR->CR3 = (PWR->CR3 & ~PWR_CR3_SMPSEN) | PWR_CR3_LDOEN;
  #else
    /* 使能 LDO */
    PWR->CR3 |= PWR_CR3_LDOEN;
  #endif /* SMPS */
  /* 等待电压等级标志置位 */
  while ((PWR->CSR1 & PWR_CSR1_ACTVOSRDY) == 0U)
  {}
#elif defined(USE_PWR_EXTERNAL_SOURCE_SUPPLY)
  #if defined(SMPS)
    PWR->CR3 = (PWR->CR3 & ~(PWR_CR3_SMPSEN | PWR_CR3_LDOEN)) | PWR_CR3_BYPASS;
  #else
    PWR->CR3 = (PWR->CR3 & ~(PWR_CR3_LDOEN)) | PWR_CR3_BYPASS;
  #endif /* SMPS */
  while ((PWR->CSR1 & PWR_CSR1_ACTVOSRDY) == 0U)
  {}
#elif defined(USE_PWR_DIRECT_SMPS_SUPPLY) && defined(SMPS)
  PWR->CR3 &= ~(PWR_CR3_LDOEN);
  while ((PWR->CSR1 & PWR_CSR1_ACTVOSRDY) == 0U)
  {}
#else
  #error "请按实际供电方式定义 USE_PWR_LDO_SUPPLY / USE_PWR_EXTERNAL_SOURCE_SUPPLY / USE_PWR_DIRECT_SMPS_SUPPLY"
#endif
}
