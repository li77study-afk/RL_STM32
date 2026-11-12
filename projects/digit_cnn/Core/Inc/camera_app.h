#ifndef CAMERA_APP_H
#define CAMERA_APP_H
#include "stm32h7xx_hal.h"
#define FRAME_WIDTH 240u
#define FRAME_HEIGHT 180u
#define FRAME_PIXELS (FRAME_WIDTH * FRAME_HEIGHT)
typedef struct {
 uint32_t stage, fatal_count, sdram_ok, sdram_error_addr, camera_id;
 uint32_t captured, displayed, dropped, copy_races, dma_errors, dcmi_errors;
 uint32_t camera_fps_x100, display_fps_x100, copy_us, display_us, max_display_us;
 uint32_t last_dma_error, last_dcmi_error, last_frame_ms, heartbeat_ms;
 uint32_t sensor_hts, sensor_vts, spi_errors;
} CameraDiagnostics;
extern volatile CameraDiagnostics g_diag;
void Board_SDRAM_Init(void);
void Camera_App_Init(void);
void Camera_App_Poll(void);
#endif
