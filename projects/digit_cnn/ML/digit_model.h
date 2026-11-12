/** 由训练脚本导出的参数接口。float 数组为只读常量，驻留片内 Flash。 */
#ifndef DIGIT_MODEL_H
#define DIGIT_MODEL_H
#include <stdint.h>
extern const float digit_conv1_weight[8*1*3*3],digit_conv1_bias[8];
extern const float digit_conv2_weight[16*8*3*3],digit_conv2_bias[16];
extern const float digit_fc_weight[10*784],digit_fc_bias[10];
#define DIGIT_SELFTEST_COUNT 10u
extern const uint8_t digit_test_images[DIGIT_SELFTEST_COUNT*784];
extern const float digit_test_logits[DIGIT_SELFTEST_COUNT*10];
#endif
