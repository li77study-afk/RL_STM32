/* 异常入口名必须与启动文件向量表对应。故障统一记录后停机，便于 SWD 调试。
 * SVC/PendSV 在本裸机工程没有任务切换用途；SysTick 负责推进 HAL 的毫秒时基。 */
#include "main.h"
#include "q_app.h"
void NMI_Handler(void) { Error_Handler(); }
void HardFault_Handler(void) { Error_Handler(); }
void MemManage_Handler(void) { Error_Handler(); }
void BusFault_Handler(void) { Error_Handler(); }
void UsageFault_Handler(void) { Error_Handler(); }
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }
/* HAL_Delay 和训练节拍都依赖 HAL_GetTick；此中断不工作会导致等待停滞。 */
void SysTick_Handler(void) { HAL_IncTick(); }
