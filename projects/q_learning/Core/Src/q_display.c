/**
 * @file q_display.c
 * @brief 将 16×16 地图和训练状态绘制到 240×320 SPI 屏。
 *
 * 先在 RAM 中画完一帧，再连续通过 SPI 发送；避免逐个像素发送指令。
 * RGB565 每像素 2 字节，整帧为 240*320*2=153600 字节。
 */
#include "q_app.h"
#include "main.h"
#include "lcd_spi_200.h"
#include <stdio.h>

/* 普通全局数组由链接脚本放到 AXI SRAM。32 字节对齐匹配 M7 Cache 行，
 * 便于按整行执行 Cache clean；当前实现由 CPU 写 SPI，没有使用 DMA。 */
__attribute__((aligned(32))) uint16_t g_framebuffer[240 * 320];
/* RGB888→RGB565：红取高 5 位、绿取高 6 位、蓝取高 5 位。 */
#define RGB(r,g,b) (uint16_t)((((r) & 248u) << 8) | (((g) & 252u) << 3) | ((b) >> 3))
#define CELL_SIZE 13
#define GRID_X 16
#define GRID_Y 66
static const uint16_t bg = RGB(12,18,29), white = RGB(230,238,248);
static const uint16_t muted = RGB(137,159,185), cyan = RGB(49,211,216);
static const uint16_t yellow = RGB(255,202,76), green = RGB(72,212,140);

/* 坐标原点在屏幕左上角。越界像素直接丢弃，防止写坏帧缓冲之外的内存。 */
static void pixel(int x, int y, uint16_t c)
{
    if (x >= 0 && x < 240 && y >= 0 && y < 320) g_framebuffer[y * 240 + x] = c;
}
static void rect(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) pixel(x+i,y+j,c);
}

/* 两种字库均为逐行存储、每行一个字节、低位对应左侧像素。
 * 标题使用 8×16，格内 S/G 使用 6×12，以便放进 13×13 的格子。
 * 只设置字模中为 1 的像素，0 位保留背景，实现透明文字。 */
static void text_font(int x, int y, const char *s, uint16_t c, const pFONT *font)
{
    for (; *s && x + font->Width <= 240; ++s, x += font->Width) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        const uint8_t *glyph = font->pTable + (ch - 32) * font->Sizes;
        for (int row = 0; row < font->Height; ++row)
            for (int col = 0; col < font->Width; ++col)
                if (glyph[row] & (1u << col)) pixel(x+col,y+row,c);
    }
}
static void text(int x, int y, const char *s, uint16_t c)
{
    text_font(x,y,s,c,&ASCII_Font16);
}

/* 用动作方向向量画箭杆，再用垂直向量画两侧箭头，无需四份图片。 */
static void arrow(int x, int y, uint32_t action, uint16_t color)
{
    static const int dx[4] = {0,1,0,-1}, dy[4] = {-1,0,1,0};
    int vx = dx[action], vy = dy[action];
    for (int i = -3; i <= 3; ++i) pixel(x+i*vx,y+i*vy,color);
    for (int i = 0; i < 3; ++i) {
        pixel(x+(3-i)*vx+i*vy,y+(3-i)*vy-i*vx,color);
        pixel(x+(3-i)*vx-i*vy,y+(3-i)*vy+i*vx,color);
    }
}

void Q_Display_Render(uint32_t mode, uint32_t state, const uint8_t *path,
                      uint32_t path_steps, uint32_t recent_success, QDisplayStats *stats)
{
    uint32_t start = DWT->CYCCNT;
    for (unsigned n = 0; n < 240u*320u; ++n) g_framebuffer[n] = bg;
    text(12,5,"Q-LEARNING",cyan);
    text(164,5,"16 x 16",muted);
    rect(12,25,216,1,RGB(48,65,85));
    char line[32];
    snprintf(line,sizeof(line),"MAP %03u/100  %s",g_current_map.id,
             mode == MODE_TRAIN ? "TRAIN" : mode == MODE_DEMO ? "DEMO" : "CHECK");
    text(12,30,line,white);
    /* 用整数拆出一位小数，避免链接体积较大的浮点 printf 格式化代码。 */
    snprintf(line,sizeof(line),"EP %4lu/%u EPS %3lu.%lu%%",(unsigned long)g_agent.episodes,QL_TRAIN_EPISODES,
             (unsigned long)g_agent.epsilon_permille/10,(unsigned long)g_agent.epsilon_permille%10);
    text(12,47,line,muted);
    for (uint32_t s = 0; s < QL_STATES; ++s) {
        int x = GRID_X + (int)(s%QL_SIDE)*CELL_SIZE;
        int y = GRID_Y + (int)(s/QL_SIDE)*CELL_SIZE;
        uint16_t fill = ql_is_wall(s) ? RGB(66,80,99) : RGB(23,34,48);
        /* 演示时只高亮模型实际走通的路径，不使用地图的 BFS 参考距离画路线。 */
        if (mode == MODE_DEMO)
            for (uint32_t k = 0; k <= path_steps; ++k)
                if (path[k] == s) fill = RGB(18,64,66);
        if (s == QL_START) fill = RGB(24,67,115);
        if (s == QL_GOAL) fill = RGB(22,101,64);
        rect(x+1,y+1,CELL_SIZE-1,CELL_SIZE-1,fill);
        /* 未访问过的格子不画箭头，避免初始全零时满屏都显示 UP。 */
        if (!ql_is_wall(s) && g_agent.visits[s] && s != QL_START && s != QL_GOAL)
            arrow(x+6,y+6,ql_greedy_action(&g_agent,s),muted);
        if (s == QL_START) text_font(x+4,y+1,"S",white,&ASCII_Font12);
        if (s == QL_GOAL) text_font(x+4,y+1,"G",green,&ASCII_Font12);
        /* 最后画智能体，确保黄色标记不会被箭头或 S/G 覆盖。 */
        if (s == state) rect(x+4,y+4,5,5,yellow);
    }
    if (mode == MODE_DEMO) snprintf(line,sizeof(line),"PATH %lu  BEST %u  WIN %lu",(unsigned long)path_steps,
                                   g_current_map.shortest_steps,(unsigned long)recent_success);
    else snprintf(line,sizeof(line),"STEP %4lu  WIN %3lu%%",(unsigned long)g_agent.steps,(unsigned long)recent_success);
    text(12,280,line,mode == MODE_DEMO ? green : white);
    text(12,300,mode == MODE_TRAIN ? "YELLOW: AGENT  GREEN: GOAL" : mode == MODE_DEMO ? "RESET TO TRAIN FROM ZERO" : "NO ROUTE: CHECK Q TABLE",muted);
    /* SPI 由 CPU 读取帧缓冲，不要求 DMA 一致性。额外 clean 是为了让 SWD
     * 截图脚本从物理 SRAM 也能读取到这帧的新像素。长度是字节数，不是像素数。 */
    SCB_CleanDCache_by_Addr((uint32_t *)g_framebuffer,sizeof(g_framebuffer));
    if (LCD_CopyBuffer(0,0,240,320,g_framebuffer) == HAL_OK) stats->frames++;
    else stats->spi_errors++;
    /* 两次 uint32_t 周期计数相减，可处理一次 DWT 回绕；帧耗时远小于周期。 */
    stats->render_us = (DWT->CYCCNT-start)/(SystemCoreClock/1000000u);
}
