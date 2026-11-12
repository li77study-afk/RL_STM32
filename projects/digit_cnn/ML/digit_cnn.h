/** 纯 C CNN 推理接口：输入为黑底亮笔画的 28×28 uint8 灰度图。 */
#ifndef DIGIT_CNN_H
#define DIGIT_CNN_H
#include <stdint.h>
#define DIGIT_INPUT_SIDE 28u
#define DIGIT_INPUT_PIXELS 784u
#define DIGIT_CLASSES 10u
typedef struct {
    float logits[DIGIT_CLASSES];       /* Softmax 前的分类分数，便于与电脑端数值对照。 */
    float probabilities[DIGIT_CLASSES];
    uint32_t digit;                    /* 概率最大的类别，0~9。 */
    float confidence;                  /* 该类别概率；不是对任意实景可靠性的保证。 */
} DigitResult;
/* 内部复用静态工作区，不可同时/递归调用。输入不被修改，不动态分配内存。 */
void DigitCNN_Run(const uint8_t image[DIGIT_INPUT_PIXELS],DigitResult *result);
/* 返回 10 个开机样本中通过“类别与数值误差”检查的数量。 */
uint32_t DigitCNN_SelfTest(float *maximum_logit_error);
#endif
