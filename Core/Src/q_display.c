#include "q_app.h"
#include "main.h"
#include "lcd_spi_200.h"
#include <stdio.h>

/* RGB565 framebuffer in AXI SRAM; no external SDRAM is needed. */
__attribute__((aligned(32))) uint16_t g_framebuffer[240 * 320];
#define RGB(r,g,b) (uint16_t)((((r) & 248u) << 8) | (((g) & 252u) << 3) | ((b) >> 3))
static const uint16_t bg = RGB(12,18,29), white = RGB(230,238,248);
static const uint16_t muted = RGB(137,159,185), cyan = RGB(49,211,216);
static const uint16_t yellow = RGB(255,202,76), green = RGB(72,212,140);

static void pixel(int x, int y, uint16_t c)
{
    if (x >= 0 && x < 240 && y >= 0 && y < 320) g_framebuffer[y * 240 + x] = c;
}
static void rect(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) pixel(x+i,y+j,c);
}
static void text(int x, int y, const char *s, uint16_t c)
{
    for (; *s && x + 8 <= 240; ++s, x += 8) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        const uint8_t *glyph = ASCII_Font16.pTable + (ch - 32) * ASCII_Font16.Sizes;
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 8; ++col)
                if (glyph[row] & (1u << col)) pixel(x+col,y+row,c);
    }
}
static void arrow(int x, int y, uint32_t action, uint16_t color)
{
    static const int dx[4] = {0,1,0,-1}, dy[4] = {-1,0,1,0};
    int vx = dx[action], vy = dy[action];
    for (int i = -4; i <= 4; ++i) pixel(x+i*vx,y+i*vy,color);
    for (int i = 0; i < 4; ++i) {
        pixel(x+(4-i)*vx+i*vy,y+(4-i)*vy-i*vx,color);
        pixel(x+(4-i)*vx-i*vy,y+(4-i)*vy+i*vx,color);
    }
}

void Q_Display_Render(uint32_t mode, uint32_t state, const uint8_t *path,
                      uint32_t path_steps, uint32_t recent_success, QDisplayStats *stats)
{
    uint32_t start = DWT->CYCCNT;
    for (unsigned n = 0; n < 240u*320u; ++n) g_framebuffer[n] = bg;
    text(12,5,"Q-LEARNING",cyan);
    text(156,5,"STM32H750",muted);
    rect(12,25,216,1,RGB(48,65,85));
    char line[32];
    snprintf(line,sizeof(line),"%s EP %4lu/%u",mode == MODE_TRAIN ? "TRAIN" : mode == MODE_DEMO ? "DEMO " : "CHECK",
             (unsigned long)g_agent.episodes,QL_TRAIN_EPISODES);
    text(12,31,line,white);
    snprintf(line,sizeof(line),"EPS %3lu.%lu%%  WIN %3lu%%",(unsigned long)g_agent.epsilon_permille/10,
             (unsigned long)g_agent.epsilon_permille%10,(unsigned long)recent_success);
    text(12,49,line,muted);
    for (uint32_t s = 0; s < QL_STATES; ++s) {
        int x = 20 + (int)(s%8)*25, y = 72 + (int)(s/8)*25;
        uint16_t fill = ql_is_wall(s) ? RGB(66,80,99) : RGB(23,34,48);
        if (mode == MODE_DEMO)
            for (uint32_t k = 0; k <= path_steps; ++k)
                if (path[k] == s) fill = RGB(18,64,66);
        if (s == QL_START) fill = RGB(24,67,115);
        if (s == QL_GOAL) fill = RGB(22,101,64);
        rect(x+1,y+1,23,23,fill);
        if (!ql_is_wall(s) && g_agent.visits[s] && s != QL_START && s != QL_GOAL)
            arrow(x+12,y+12,ql_greedy_action(&g_agent,s),muted);
        if (s == QL_START) text(x+9,y+5,"S",white);
        if (s == QL_GOAL) text(x+9,y+5,"G",green);
        if (s == state) rect(x+8,y+8,9,9,yellow);
    }
    if (mode == MODE_DEMO) snprintf(line,sizeof(line),"GREEDY PATH: %lu STEPS",(unsigned long)path_steps);
    else snprintf(line,sizeof(line),"STEP %3lu  RETURN %5ld",(unsigned long)g_agent.steps,(long)g_agent.episode_return);
    text(12,279,line,mode == MODE_DEMO ? green : white);
    text(12,300,mode == MODE_TRAIN ? "YELLOW: AGENT  GREEN: GOAL" : mode == MODE_DEMO ? "RESET TO TRAIN FROM ZERO" : "NO ROUTE: CHECK Q TABLE",muted);
    /* CPU writes SPI directly. Clean additionally makes SWD snapshots coherent. */
    SCB_CleanDCache_by_Addr((uint32_t *)g_framebuffer,sizeof(g_framebuffer));
    if (LCD_CopyBuffer(0,0,240,320,g_framebuffer) == HAL_OK) stats->frames++;
    else stats->spi_errors++;
    stats->render_us = (DWT->CYCCNT-start)/(SystemCoreClock/1000000u);
}
