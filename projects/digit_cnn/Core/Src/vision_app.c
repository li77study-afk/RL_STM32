/**
 * @file vision_app.c
 * @brief 单数字识别、拒识、显示、开机样本自检与 SWD 诊断。
 *
 * 视觉算法读的是已经从 DMA 帧复制出的稳定图像。它不修改相机原始帧，
 * 取景框仅在预处理和推理完成后叠加到屏幕副本，避免绿框成为数字笔画。
 */
#include "vision_app.h"
#include "camera_app.h"
#include "digit_cnn.h"
#include "digit_model.h"
#include "digit_preprocess.h"
#include "lcd_spi_200.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

__attribute__((section(".diagnostics"),aligned(32))) volatile VisionStatus g_status;
__attribute__((section(".diagnostics"),aligned(32))) volatile uint32_t g_command;
__attribute__((section(".diagnostics"),aligned(32))) volatile uint8_t g_test_image[784];
__attribute__((aligned(32))) uint8_t g_digit_input[784];
extern uint16_t display_frame[FRAME_PIXELS];
static DigitPreprocessInfo pre;
static DigitResult result;
static uint32_t infer_tick,ui_tick,publish_tick,mode,requests,frozen;
static uint32_t infer_count,infer_us,pre_us,recognized,candidate,votes;
static uint32_t selftest_passed,selftest_error_micro;
static uint16_t thumbnail[56*56];

static uint32_t elapsed_us(uint32_t start)
{
    return (DWT->CYCCNT-start)/(SystemCoreClock/1000000u);
}

static void predict(void)
{
    uint32_t start=DWT->CYCCNT;
    DigitCNN_Run(g_digit_input,&result);
    infer_us=elapsed_us(start);infer_count++;
}

void Vision_Init(void)
{
    memset((void *)&g_status,0,sizeof(g_status));
    memset(g_digit_input,0,sizeof(g_digit_input));
    memset(&result,0,sizeof(result));memset(&pre,0,sizeof(pre));
    g_command=0;mode=0;requests=0;frozen=0;recognized=VISION_NO_DIGIT;
    candidate=VISION_NO_DIGIT;votes=0;infer_count=0;infer_us=0;pre_us=0;
    g_status.magic=VISION_MAGIC;g_status.version=VISION_PROTOCOL_VERSION;
    /* 在接入摄像头之前，用真实测试样本验证权重顺序、算子和 FPU 数值。
     * 这里不经过预处理，故“模型自检通过”不能替代纸面实拍准确率测试。 */
    LCD_DisplayString(0,8,"DIGIT CNN SELF TEST");
    float maximum_error=0;
    selftest_passed=DigitCNN_SelfTest(&maximum_error);
    selftest_error_micro=(uint32_t)(maximum_error*1000000.0f+0.5f);
    g_status.selftest_passed=selftest_passed;g_status.selftest_total=DIGIT_SELFTEST_COUNT;
    if(selftest_passed!=DIGIT_SELFTEST_COUNT) {
        LCD_DisplayString(0,32,"CNN SELF TEST FAILED");
        g_status.stage=g_diag.stage;g_status.fatal_count=1;Error_Handler();
    }
    LCD_SetBackColor(LCD_BLACK);LCD_Clear();
    LCD_DisplayString(0,0,"HANDWRITTEN DIGIT CNN");
    infer_tick=HAL_GetTick()-100u;ui_tick=HAL_GetTick()-200u;publish_tick=HAL_GetTick()-100u;
}

void Vision_ProcessFrame(const uint16_t *frame)
{
    uint32_t now=HAL_GetTick();
    if(mode||now-infer_tick<100u)return; /* 相机识别最多约 10 Hz，预览单独调度。 */
    infer_tick=now;
    uint32_t start=DWT->CYCCNT;
    int valid=DigitPreprocess_RGB565(frame,FRAME_WIDTH,FRAME_HEIGHT,g_digit_input,&pre);
    pre_us=elapsed_us(start);
    if(!valid) {recognized=VISION_NO_DIGIT;candidate=VISION_NO_DIGIT;votes=0;return;}
    predict();
    /* 除前景检查外，再要求概率和前两名差距，减少模糊笔画/短暂抖动。
     * 这些阈值是演示参数，不保证把所有非数字物体正确拒识。 */
    float second=0;
    for(unsigned c=0;c<10;c++)if(c!=result.digit&&result.probabilities[c]>second)second=result.probabilities[c];
    if(result.confidence<0.80f||result.confidence-second<0.15f) {
        recognized=VISION_NO_DIGIT;candidate=VISION_NO_DIGIT;votes=0;return;
    }
    if(candidate==result.digit)votes++;else {candidate=result.digit;votes=1;}
    recognized=votes>=2?candidate:VISION_NO_DIGIT;
}

void Vision_PollCommands(void)
{
    if(g_command==1) {
        for(unsigned n=0;n<784;n++)g_digit_input[n]=g_test_image[n];
        mode=1;memset(&pre,0,sizeof(pre));pre_us=0;
        predict();recognized=result.digit;requests++;__DMB();g_command=0;
    } else if(g_command==2) {
        mode=0;recognized=VISION_NO_DIGIT;candidate=VISION_NO_DIGIT;votes=0;
        infer_tick=HAL_GetTick()-100u;requests++;__DMB();g_command=0;
    } else if(g_command==3) {
        /* 停止更新 CPU 副本，再清理 D-Cache，使 SWD 可以读取物理 SRAM 中
         * 一致的完整帧。只冻结预览，不停 CPU、不停相机 DMA。 */
        frozen=1;
        SCB_CleanDCache_by_Addr((uint32_t *)display_frame,sizeof(uint16_t)*FRAME_PIXELS);
        SCB_CleanDCache_by_Addr((uint32_t *)g_digit_input,800);
        requests++;__DMB();g_command=0;
    } else if(g_command==4) {
        frozen=0;requests++;__DMB();g_command=0;
    }
}

uint32_t Vision_IsFrozen(void) {return frozen;}

void Vision_DrawOverlay(uint16_t *frame)
{
    /* RGB565 青绿色取景框。坐标相对于 240×180 相机副本，不包括屏幕的 y=60 偏移。 */
    uint16_t color=recognized<10?0x07e0u:0x07ffu;
    unsigned x0=DIGIT_ROI_X,y0=DIGIT_ROI_Y,x1=x0+DIGIT_ROI_SIDE-1,y1=y0+DIGIT_ROI_SIDE-1;
    for(unsigned x=x0;x<=x1;x++) {frame[y0*FRAME_WIDTH+x]=color;frame[y1*FRAME_WIDTH+x]=color;}
    for(unsigned y=y0;y<=y1;y++) {frame[y*FRAME_WIDTH+x0]=color;frame[y*FRAME_WIDTH+x1]=color;}
}

void Vision_DrawStatus(void)
{
    uint32_t now=HAL_GetTick();if(now-ui_tick<200u)return;ui_tick=now;
    LCD_SetBackColor(LCD_BLACK);LCD_SetColor(LCD_WHITE);LCD_SetAsciiFont(&ASCII_Font16);
    char line[40];
    LCD_ClearRect(0,20,240,36);
    if(recognized<10)snprintf(line,sizeof(line),"%s: %lu  CONF %lu.%lu%%",mode?"TEST":"DIGIT",(unsigned long)recognized,
        (unsigned long)(result.confidence*1000)/10,(unsigned long)(result.confidence*1000)%10);
    else snprintf(line,sizeof(line),"DIGIT: --   %s",pre.status==PREPROCESS_OK?"HOLD STILL":"PAPER IN BOX");
    LCD_DisplayString(0,20,line);
    snprintf(line,sizeof(line),"SELFTEST %lu/10 %s",(unsigned long)selftest_passed,mode?"TEST":"LIVE");
    LCD_DisplayString(0,38,line);
    LCD_ClearRect(0,244,168,74);
    snprintf(line,sizeof(line),"CNN %lu.%lu ms",(unsigned long)infer_us/1000,(unsigned long)infer_us%1000/100);
    LCD_DisplayString(0,246,line);
    snprintf(line,sizeof(line),"PRE %lu.%lu ms",(unsigned long)pre_us/1000,(unsigned long)pre_us%1000/100);
    LCD_DisplayString(0,264,line);
    snprintf(line,sizeof(line),"LCD %lu.%02lu FPS",(unsigned long)g_diag.display_fps_x100/100,(unsigned long)g_diag.display_fps_x100%100);
    LCD_DisplayString(0,282,line);
    LCD_DisplayString(0,300,"28x28 INPUT ->");
    for(unsigned y=0;y<56;y++)for(unsigned x=0;x<56;x++) {
        unsigned value=g_digit_input[(y/2)*28+x/2];
        thumbnail[y*56+x]=(uint16_t)(((value&248u)<<8)|((value&252u)<<3)|(value>>3));
    }
    if(LCD_CopyBuffer(176,252,56,56,thumbnail)!=HAL_OK)g_diag.spi_errors++;
}

void Vision_Publish(void)
{
    uint32_t now=HAL_GetTick();if(now-publish_tick<100u)return;publish_tick=now;
    g_status.sequence++;__DMB();
    g_status.stage=g_diag.stage;g_status.fatal_count=g_diag.fatal_count;g_status.heartbeat_ms=now;
    g_status.sdram_ok=g_diag.sdram_ok;g_status.camera_id=g_diag.camera_id;
    g_status.captured=g_diag.captured;g_status.displayed=g_diag.displayed;g_status.dropped=g_diag.dropped;
    g_status.copy_races=g_diag.copy_races;g_status.dma_errors=g_diag.dma_errors;g_status.dcmi_errors=g_diag.dcmi_errors;
    g_status.spi_errors=g_diag.spi_errors;g_status.preview_fps_x100=g_diag.display_fps_x100;
    g_status.copy_us=g_diag.copy_us;g_status.display_us=g_diag.display_us;
    g_status.inference_count=infer_count;g_status.inference_us=infer_us;g_status.preprocess_us=pre_us;
    g_status.raw_digit=result.digit;g_status.confidence_permille=(uint32_t)(result.confidence*1000+0.5f);
    g_status.recognized_digit=recognized;g_status.preprocess_status=pre.status;
    g_status.threshold=pre.threshold;g_status.contrast=pre.contrast;g_status.foreground=pre.foreground;
    g_status.bbox_x=pre.bbox_x;g_status.bbox_y=pre.bbox_y;g_status.bbox_w=pre.bbox_w;g_status.bbox_h=pre.bbox_h;
    g_status.selftest_passed=selftest_passed;g_status.selftest_total=DIGIT_SELFTEST_COUNT;g_status.selftest_max_error_micro=selftest_error_micro;
    g_status.input_mode=mode;g_status.processed_requests=requests;
    g_status.model_parameters=9098;g_status.model_macs=290080;g_status.core_hz=SystemCoreClock;
    g_status.frame_frozen=frozen;
    __DMB();g_status.sequence++;
}
