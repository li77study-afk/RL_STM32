/** 摄像头预处理：白纸深色单数字 → 黑底亮笔画 28×28，保持训练输入约定。 */
#ifndef DIGIT_PREPROCESS_H
#define DIGIT_PREPROCESS_H
#include <stdint.h>
#define DIGIT_ROI_SIDE 128u
#define DIGIT_ROI_X 56u
#define DIGIT_ROI_Y 26u
enum {
    PREPROCESS_OK=0, PREPROCESS_LOW_CONTRAST=1, PREPROCESS_NO_FOREGROUND=2,
    PREPROCESS_TOO_LARGE=3, PREPROCESS_TOUCHES_EDGE=4, PREPROCESS_BAD_ARGUMENT=5
};
typedef struct {
    uint32_t status, threshold, contrast, foreground;
    uint32_t bbox_x, bbox_y, bbox_w, bbox_h;
} DigitPreprocessInfo;
/* 帧必须是 CPU 持有的稳定副本；函数不会改写原帧。输出无效时自动清零。
 * 参数 width/height 至少覆盖固定 ROI；算法含静态工作区，不可重入。
 * 前景/置信度拒识用于改善演示，不构成通用“是否为数字”的可靠检测器。 */
int DigitPreprocess_RGB565(const uint16_t *frame,uint32_t width,uint32_t height,
                          uint8_t output[784],DigitPreprocessInfo *info);
#endif
