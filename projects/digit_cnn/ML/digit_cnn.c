/**
 * @file digit_cnn.c
 * @brief 手写 FP32 CNN：零填充卷积→ReLU→最大池化→全连接→Softmax。
 *
 * 所有特征均为 CHW（通道、行、列）布局，与训练脚本 PyTorch 的顺序一致。
 * 卷积权重为 OIHW：输出通道、输入通道、核行、核列。
 * 本首版优先保证代码可读和数值正确，后续可在同一接口替换为 INT8 算子。
 */
#include "digit_cnn.h"
#include "digit_model.h"
#include <math.h>

#if defined(__arm__) || defined(__thumb__)
#define CNN_WORKSPACE __attribute__((section(".cnn_workspace"),aligned(32)))
#else
#define CNN_WORKSPACE
#endif
/* 两个乒乓工作区共 31,360 字节。B 最初存输入，之后反复存池化结果。
 * ARM 上放入 DTCM；NOLOAD 段不自动清零，但每个有效元素均在读取前被写入。 */
static CNN_WORKSPACE float work_a[8*28*28];
static CNN_WORKSPACE float work_b[8*14*14];

static void conv3_relu(const float *input,float *output,unsigned side,
                       unsigned in_channels,unsigned out_channels,
                       const float *weights,const float *bias)
{
    unsigned plane=side*side;
    for(unsigned oc=0;oc<out_channels;oc++) {
        for(unsigned y=0;y<side;y++) for(unsigned x=0;x<side;x++) {
            float sum=bias[oc];
            for(unsigned ic=0;ic<in_channels;ic++) {
                const float *kernel=weights+(oc*in_channels+ic)*9;
                for(int ky=0;ky<3;ky++) {
                    int iy=(int)y+ky-1;
                    if(iy<0||iy>=(int)side) continue; /* same padding：越界值为 0。 */
                    for(int kx=0;kx<3;kx++) {
                        int ix=(int)x+kx-1;
                        if(ix>=0&&ix<(int)side)
                            sum+=input[ic*plane+(unsigned)iy*side+(unsigned)ix]*kernel[ky*3+kx];
                    }
                }
            }
            output[oc*plane+y*side+x]=sum>0.0f?sum:0.0f;
        }
    }
}

static void maxpool2(const float *input,float *output,unsigned side,unsigned channels)
{
    unsigned next=side/2;
    for(unsigned c=0;c<channels;c++) for(unsigned y=0;y<next;y++) for(unsigned x=0;x<next;x++) {
        unsigned start=c*side*side+(y*2)*side+x*2;
        float best=input[start];
        if(input[start+1]>best) best=input[start+1];
        if(input[start+side]>best) best=input[start+side];
        if(input[start+side+1]>best) best=input[start+side+1];
        output[c*next*next+y*next+x]=best;
    }
}

void DigitCNN_Run(const uint8_t image[DIGIT_INPUT_PIXELS],DigitResult *result)
{
    for(unsigned n=0;n<DIGIT_INPUT_PIXELS;n++) work_b[n]=(float)image[n]*(1.0f/255.0f);
    conv3_relu(work_b,work_a,28,1,8,digit_conv1_weight,digit_conv1_bias);
    maxpool2(work_a,work_b,28,8);
    conv3_relu(work_b,work_a,14,8,16,digit_conv2_weight,digit_conv2_bias);
    maxpool2(work_a,work_b,14,16);
    /* 池化输出为 16*7*7=784 个数；CHW 连续布局可直接用于全连接。 */
    unsigned best=0;
    for(unsigned c=0;c<DIGIT_CLASSES;c++) {
        float sum=digit_fc_bias[c];
        for(unsigned n=0;n<784;n++) sum+=digit_fc_weight[c*784+n]*work_b[n];
        result->logits[c]=sum;
        if(sum>result->logits[best]) best=c;
    }
    /* 减去最大 logit 再取指数，避免 exp 溢出；不影响概率比例或 argmax。 */
    float total=0.0f;
    for(unsigned c=0;c<DIGIT_CLASSES;c++) {
        result->probabilities[c]=expf(result->logits[c]-result->logits[best]);
        total+=result->probabilities[c];
    }
    for(unsigned c=0;c<DIGIT_CLASSES;c++) result->probabilities[c]/=total;
    result->digit=best;result->confidence=result->probabilities[best];
}

uint32_t DigitCNN_SelfTest(float *maximum_logit_error)
{
    uint32_t passed=0;float worst=0.0f;
    for(unsigned n=0;n<DIGIT_SELFTEST_COUNT;n++) {
        DigitResult result;DigitCNN_Run(digit_test_images+n*784,&result);
        float error=0.0f;
        for(unsigned c=0;c<10;c++) {
            float difference=fabsf(result.logits[c]-digit_test_logits[n*10+c]);
            if(difference>error) error=difference;
        }
        if(error>worst) worst=error;
        if(result.digit==n&&error<0.001f) passed++;
    }
    if(maximum_logit_error) *maximum_logit_error=worst;
    return passed;
}
