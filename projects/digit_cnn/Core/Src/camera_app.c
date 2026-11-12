#include "camera_app.h"
#include "main.h"
#include "dcmi_ov5640.h"
#include "lcd_spi_200.h"
#include "led.h"
#include "vision_app.h"
#include <string.h>
#include <stdio.h>

extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef hdma_dcmi;
extern SPI_HandleTypeDef hspi6;
__attribute__((section(".diagnostics"),aligned(32)))
volatile CameraDiagnostics g_diag;
__attribute__((section(".sdram"),aligned(32)))
uint16_t camera_frames[2][FRAME_PIXELS];
/* CPU copies from non-cacheable SDRAM into cacheable AXI memory before SPI.
 * The DMA continues alternating in SDRAM; the display never owns a DMA buffer. */
__attribute__((aligned(32))) uint16_t display_frame[FRAME_PIXELS];
static volatile uint32_t ready_sequence, ready_index;
static uint32_t consumed_sequence, report_tick, report_capture, report_display, preview_tick;

static uint32_t elapsed_us(uint32_t start) {
 return (DWT->CYCCNT-start)/(SystemCoreClock/1000000u);
}
static void DMA_FrameDone(DMA_HandleTypeDef *dma) {
 ready_index=(((DMA_Stream_TypeDef *)dma->Instance)->CR & DMA_SxCR_CT)?0u:1u;
 __DMB(); ready_sequence++; g_diag.captured++;
 g_diag.last_frame_ms=HAL_GetTick();
}
static void DMA_Failed(DMA_HandleTypeDef *dma) {
 g_diag.dma_errors++;g_diag.last_dma_error=dma->ErrorCode;
}
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *camera) {(void)camera;}
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *camera) {
 g_diag.dcmi_errors++;g_diag.last_dcmi_error=camera->ErrorCode;
}
static void reg16(uint16_t addr,uint16_t value) {
 SCCB_WriteReg_16Bit(addr,value>>8);SCCB_WriteReg_16Bit(addr+1,value&255);
}
static void Sensor_FastMode(void) {
 /* 1920x1440 centered sensor ROI, 2x binning, ISP scales to 240x180.
  * 80 MHz internal timing / (1800*741) ~59.98 fps. Validate on hardware.
  * Explicit PCLK divider 4 keeps the DVP clock below the 80 MHz DCMI limit. */
 SCCB_WriteReg_16Bit(0x3008,0x42);
 reg16(0x3800,336);reg16(0x3802,252);
 reg16(0x3804,2255);reg16(0x3806,1691);
 reg16(0x3808,FRAME_WIDTH);reg16(0x380a,FRAME_HEIGHT);
 reg16(0x380c,1800);reg16(0x380e,741);
 reg16(0x3810,16);reg16(0x3812,4);
 SCCB_WriteReg_16Bit(0x3814,0x31);SCCB_WriteReg_16Bit(0x3815,0x31);
 SCCB_WriteReg_16Bit(0x460c,0x22);SCCB_WriteReg_16Bit(0x3824,4);
 /* Disable night-mode frame extension but retain automatic exposure/gain. */
 SCCB_WriteReg_16Bit(0x3a00,SCCB_ReadReg_16Bit(0x3a00)&~4u);
 reg16(0x3a02,737);reg16(0x3a14,737);
 SCCB_WriteReg_16Bit(0x3008,0x02);
 HAL_Delay(100);
 g_diag.sensor_hts=(SCCB_ReadReg_16Bit(0x380c)<<8)|SCCB_ReadReg_16Bit(0x380d);
 g_diag.sensor_vts=(SCCB_ReadReg_16Bit(0x380e)<<8)|SCCB_ReadReg_16Bit(0x380f);
}
void Camera_App_Init(void) {
 LCD_SetAsciiFont(&ASCII_Font16);LCD_SetDirection(Direction_V);LCD_SetBackColor(LCD_BLACK);LCD_SetColor(LCD_WHITE);LCD_Clear();
 Vision_Init();
 g_diag.stage=5;
 if(DCMI_OV5640_Init()!=OV5640_Success) {
  g_diag.camera_id=OV5640_ReadID();LCD_DisplayString(0,40,"CAMERA ID ERROR");Error_Handler();
 }
 g_diag.camera_id=OV5640_ReadID(); g_diag.stage=6;
 Sensor_FastMode();HAL_DCMI_DisableCrop(&hdcmi);
 memset(camera_frames,0,sizeof(camera_frames));
 hdma_dcmi.XferCpltCallback=DMA_FrameDone;
 hdma_dcmi.XferM1CpltCallback=DMA_FrameDone;
 hdma_dcmi.XferErrorCallback=DMA_Failed;
 hdma_dcmi.XferHalfCpltCallback=NULL;hdma_dcmi.XferM1HalfCpltCallback=NULL;
 hdcmi.State=HAL_DCMI_STATE_BUSY;hdcmi.ErrorCode=HAL_DCMI_ERROR_NONE;
 __HAL_DCMI_ENABLE(&hdcmi);
 CLEAR_BIT(DCMI->CR,DCMI_CR_CM);
 DCMI->ICR=0x1f;
 __HAL_DCMI_DISABLE_IT(&hdcmi,DCMI_IT_FRAME|DCMI_IT_LINE|DCMI_IT_VSYNC);
 __HAL_DCMI_ENABLE_IT(&hdcmi,DCMI_IT_OVR|DCMI_IT_ERR);
 if(HAL_DMAEx_MultiBufferStart_IT(&hdma_dcmi,(uint32_t)&DCMI->DR,
    (uint32_t)camera_frames[0],(uint32_t)camera_frames[1],FRAME_PIXELS/2)!=HAL_OK) Error_Handler();
 SET_BIT(DCMI->CR,DCMI_CR_CAPTURE);
 report_tick=HAL_GetTick();preview_tick=report_tick-33u;g_diag.stage=7;
}
/** 相机持续采集，预览最多约 30 FPS，CNN 最多约 10 Hz，避免串行挤满每帧预算。 */
void Camera_App_Poll(void) {
 uint32_t now=HAL_GetTick();g_diag.heartbeat_ms=now;
 Vision_PollCommands();
 uint32_t seq=ready_sequence;
 if(!Vision_IsFrozen() && seq!=consumed_sequence && now-preview_tick>=33u) {
  uint32_t index=ready_index;__DMB();
  uint32_t active=(((DMA_Stream_TypeDef *)hdma_dcmi.Instance)->CR & DMA_SxCR_CT)?1u:0u;
  if(index==active || seq!=ready_sequence) return;
  uint32_t start=DWT->CYCCNT;
  memcpy(display_frame,camera_frames[index],sizeof(display_frame));__DMB();
  g_diag.copy_us=elapsed_us(start);
  if(seq!=ready_sequence || index==((((DMA_Stream_TypeDef *)hdma_dcmi.Instance)->CR&DMA_SxCR_CT)?1u:0u)) {
   g_diag.copy_races++;return;
  }
  /* 消费最新完整帧。因主动降帧或推理而跳过旧帧属正常调度，不是 DMA 错误。 */
  g_diag.dropped+=seq-consumed_sequence-1;consumed_sequence=seq;preview_tick=now;
  Vision_ProcessFrame(display_frame);
  Vision_DrawOverlay(display_frame);
  start=DWT->CYCCNT;
  HAL_StatusTypeDef result=LCD_CopyBuffer(0,60,FRAME_WIDTH,FRAME_HEIGHT,display_frame);
  g_diag.display_us=elapsed_us(start);
  if(g_diag.display_us>g_diag.max_display_us)g_diag.max_display_us=g_diag.display_us;
  if(result!=HAL_OK)g_diag.spi_errors++;else g_diag.displayed++;
 }
 Vision_DrawStatus();
 now=HAL_GetTick();
 if(now-report_tick>=1000u) {
  uint32_t dt=now-report_tick,cap=g_diag.captured,disp=g_diag.displayed;
  g_diag.camera_fps_x100=(cap-report_capture)*100000u/dt;
  g_diag.display_fps_x100=(disp-report_display)*100000u/dt;
  report_capture=cap;report_display=disp;report_tick=now;LED1_Toggle;
 }
 Vision_Publish();
 HAL_Delay(1);
}
