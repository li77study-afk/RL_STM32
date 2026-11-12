/**
 * @file digit_preprocess.c
 * @brief 定位单个深色连通笔画、等比例缩放、灰度反色和重心对齐。
 *
 * 阈值只用于选择包围框。送给 CNN 的仍是灰度图，保留边缘灰度信息，
 * 避免把抗锯齿笔画全部变成生硬的 0/255。只支持白纸上独立的单个深色数字。
 */
#include "digit_preprocess.h"
#include <string.h>

#define SIDE DIGIT_ROI_SIDE
#define PIXELS (SIDE*SIDE)
static uint8_t gray[PIXELS],seen[PIXELS],centered[784];
static uint16_t queue[PIXELS]; /* 128²=16384 个下标，可用 uint16_t。 */
static uint32_t histogram[256];

static uint8_t luminance(uint16_t rgb)
{
    unsigned r=((rgb>>11)&31u)*255u/31u;
    unsigned g=((rgb>>5)&63u)*255u/63u;
    unsigned b=(rgb&31u)*255u/31u;
    return (uint8_t)((77u*r+150u*g+29u*b)>>8);
}

static unsigned otsu(void)
{
    /* 最大类间方差分割：只遍历 256 个灰度等级。
     * 用 float 避免 计数²×灰度差² 的整型溢出。 */
    uint32_t total_sum=0,left_count=0,left_sum=0;
    for(unsigned t=0;t<256;t++) total_sum+=t*histogram[t];
    float best=-1.0f;unsigned threshold=0;
    for(unsigned t=0;t<255;t++) {
        left_count+=histogram[t];left_sum+=t*histogram[t];
        unsigned right_count=PIXELS-left_count;
        if(!left_count||!right_count) continue;
        float difference=(float)left_sum/left_count-(float)(total_sum-left_sum)/right_count;
        float variance=(float)left_count*(float)right_count*difference*difference;
        if(variance>best) {best=variance;threshold=t;}
    }
    return threshold;
}

static float sample(float x,float y)
{
    /* 双线性采样，在 ROI 边缘钳位。 */
    if(x<0)x=0;
    if(y<0)y=0;
    if(x>SIDE-1)x=SIDE-1;
    if(y>SIDE-1)y=SIDE-1;
    unsigned x0=(unsigned)x,y0=(unsigned)y;
    unsigned x1=x0+1<SIDE?x0+1:x0,y1=y0+1<SIDE?y0+1:y0;
    float dx=x-(float)x0,dy=y-(float)y0;
    return (1-dy)*((1-dx)*gray[y0*SIDE+x0]+dx*gray[y0*SIDE+x1])+
           dy*((1-dx)*gray[y1*SIDE+x0]+dx*gray[y1*SIDE+x1]);
}

int DigitPreprocess_RGB565(const uint16_t *frame,uint32_t width,uint32_t height,
                          uint8_t output[784],DigitPreprocessInfo *info)
{
    memset(output,0,784);memset(info,0,sizeof(*info));
    if(!frame||width<DIGIT_ROI_X+SIDE||height<DIGIT_ROI_Y+SIDE) {
        info->status=PREPROCESS_BAD_ARGUMENT;return 0;
    }
    memset(histogram,0,sizeof(histogram));memset(seen,0,sizeof(seen));
    unsigned darkest=255,brightest=0;
    for(unsigned y=0;y<SIDE;y++) for(unsigned x=0;x<SIDE;x++) {
        unsigned value=luminance(frame[(y+DIGIT_ROI_Y)*width+x+DIGIT_ROI_X]);
        gray[y*SIDE+x]=(uint8_t)value;histogram[value]++;
        if(value<darkest)darkest=value;
        if(value>brightest)brightest=value;
    }
    info->contrast=brightest-darkest;
    if(info->contrast<40) {info->status=PREPROCESS_LOW_CONTRAST;return 0;}
    unsigned threshold=otsu();info->threshold=threshold;
    unsigned largest=0,bx0=0,by0=0,bx1=0,by1=0;
    /* 8 邻域连通域搜索能保持斜笔画连通。只保留最大深色笔画的包围框，
     * 避免孤立噪点把整个纸面都圈进输入。纸上应只放一个数字。 */
    for(unsigned start=0;start<PIXELS;start++) {
        if(seen[start]||gray[start]>threshold)continue;
        unsigned head=0,tail=0,minx=SIDE,miny=SIDE,maxx=0,maxy=0;
        queue[tail++]=(uint16_t)start;seen[start]=1;
        while(head<tail) {
            unsigned pos=queue[head++],y=pos/SIDE,x=pos%SIDE;
            if(x<minx)minx=x;
            if(x>maxx)maxx=x;
            if(y<miny)miny=y;
            if(y>maxy)maxy=y;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
                int nx=(int)x+dx,ny=(int)y+dy;
                if(nx<0||ny<0||nx>=(int)SIDE||ny>=(int)SIDE)continue;
                unsigned next=(unsigned)ny*SIDE+(unsigned)nx;
                if(!seen[next]&&gray[next]<=threshold) {seen[next]=1;queue[tail++]=(uint16_t)next;}
            }
        }
        if(tail>largest) {largest=tail;bx0=minx;by0=miny;bx1=maxx;by1=maxy;}
    }
    info->foreground=largest;
    if(largest<20||by1-by0+1<12||bx1-bx0+1<3) {info->status=PREPROCESS_NO_FOREGROUND;return 0;}
    info->bbox_x=bx0;info->bbox_y=by0;info->bbox_w=bx1-bx0+1;info->bbox_h=by1-by0+1;
    if(largest>PIXELS*45u/100u) {info->status=PREPROCESS_TOO_LARGE;return 0;}
    if(bx0<2||by0<2||bx1>=SIDE-2||by1>=SIDE-2) {info->status=PREPROCESS_TOUCHES_EDGE;return 0;}
    /* 模仿 MNIST 的 20×20 内容框，再居中补到 28×28。保留纵横比，避免
     * 把窄的“1”横向拉宽。灰度反色后：纸为 0，深色笔画趋向 255。 */
    unsigned w=info->bbox_w,h=info->bbox_h;
    float scale=20.0f/(float)(w>h?w:h);
    unsigned out_w=(unsigned)((float)w*scale+0.5f),out_h=(unsigned)((float)h*scale+0.5f);
    if(!out_w)out_w=1;
    if(!out_h)out_h=1;
    unsigned ox=(28-out_w)/2,oy=(28-out_h)/2;
    unsigned accumulated=0,background=255;
    for(unsigned t=0;t<256;t++) {accumulated+=histogram[t];if(accumulated>=PIXELS*9u/10u){background=t;break;}}
    float range=(float)background-(float)darkest;if(range<1)range=1;
    for(unsigned y=0;y<out_h;y++)for(unsigned x=0;x<out_w;x++) {
        float sx=(float)bx0+((float)x+0.5f)*(float)w/(float)out_w-0.5f;
        float sy=(float)by0+((float)y+0.5f)*(float)h/(float)out_h-0.5f;
        float value=((float)background-sample(sx,sy))*255.0f/range;
        if(value<0)value=0;
        if(value>255)value=255;
        output[(y+oy)*28+x+ox]=(uint8_t)(value+0.5f);
    }
    /* MNIST 按灰度重心居中。最多再平移 4 像素，防止特殊噪声造成极端偏移。 */
    uint32_t mass=0,mx=0,my=0;
    for(unsigned y=0;y<28;y++)for(unsigned x=0;x<28;x++) {
        unsigned value=output[y*28+x];mass+=value;mx+=x*value;my+=y*value;
    }
    if(mass) {
        float fx=13.5f-(float)mx/(float)mass,fy=13.5f-(float)my/(float)mass;
        int dx=(int)(fx+(fx>=0?0.5f:-0.5f)),dy=(int)(fy+(fy>=0?0.5f:-0.5f));
        if(dx>4)dx=4;
        if(dx< -4)dx= -4;
        if(dy>4)dy=4;
        if(dy< -4)dy= -4;
        memset(centered,0,sizeof(centered));
        for(int y=0;y<28;y++)for(int x=0;x<28;x++)if(x+dx>=0&&x+dx<28&&y+dy>=0&&y+dy<28)
            centered[(y+dy)*28+x+dx]=output[y*28+x];
        memcpy(output,centered,784);
    }
    info->status=PREPROCESS_OK;return 1;
}
