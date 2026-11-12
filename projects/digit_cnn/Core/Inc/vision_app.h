/** 图像识别应用：独立发布固定 32 位布局，方便 SWD 监测与自动测试。 */
#ifndef VISION_APP_H
#define VISION_APP_H
#include <stdint.h>
#define VISION_MAGIC 0x434E4E31u
#define VISION_PROTOCOL_VERSION 1u
#define VISION_NO_DIGIT 10u
typedef struct {
    uint32_t sequence,magic,version,stage,fatal_count,heartbeat_ms;
    uint32_t sdram_ok,camera_id,captured,displayed,dropped,copy_races;
    uint32_t dma_errors,dcmi_errors,spi_errors,preview_fps_x100;
    uint32_t copy_us,display_us,inference_count,inference_us,preprocess_us;
    uint32_t raw_digit,confidence_permille,recognized_digit,preprocess_status;
    uint32_t threshold,contrast,foreground,bbox_x,bbox_y,bbox_w,bbox_h;
    uint32_t selftest_passed,selftest_total,selftest_max_error_micro;
    uint32_t input_mode,processed_requests,model_parameters,model_macs,core_hz,frame_frozen;
} VisionStatus;
extern volatile VisionStatus g_status;
/* 命令 1：读取 g_test_image 并独立推理，保持 TEST 模式；命令 2：恢复相机。
 * 调试器必须先完整写图，再写命令。应用读完后清零命令。
 * 命令 3/4：冻结/恢复 CPU 预览副本，供截图脚本读取一致图像；相机 DMA 不停止。
 * 这是自动化验证接口，不参与正常摄像头输入。 */
extern volatile uint32_t g_command;
extern volatile uint8_t g_test_image[784];
extern uint8_t g_digit_input[784];
void Vision_Init(void);
void Vision_ProcessFrame(const uint16_t *frame);
void Vision_PollCommands(void);
void Vision_DrawOverlay(uint16_t *frame);
void Vision_DrawStatus(void);
void Vision_Publish(void);
uint32_t Vision_IsFrozen(void);
#endif
