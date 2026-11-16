/** 240×320 RGB565 软件绘图：地形、坦克、飞行弹丸、学习进度和胜率。 */
#include "tank_app.h"
#include "lcd_spi_200.h"
#include "main.h"
#include <stdio.h>

__attribute__((aligned(32))) uint16_t g_framebuffer[240*320];
#define RGB(r,g,b) (uint16_t)((((r)&248u)<<8)|(((g)&252u)<<3)|((b)>>3))
#define CELL 13
#define GX 16
#define GY 65
static const uint16_t bg=RGB(9,17,28),white=RGB(231,239,249),muted=RGB(133,155,178);
static const uint16_t blue=RGB(49,184,255),red=RGB(255,85,102),gold=RGB(255,207,74),green=RGB(73,215,149);

static void pixel(int x,int y,uint16_t color)
{
    if(x>=0&&x<240&&y>=0&&y<320)g_framebuffer[y*240+x]=color;
}
static void rect(int x,int y,int w,int h,uint16_t color)
{
    for(int j=0;j<h;j++)for(int i=0;i<w;i++)pixel(x+i,y+j,color);
}
static void text(int x,int y,const char *s,uint16_t color,const pFONT *font)
{
    for(;*s&&x+font->Width<=240;s++,x+=font->Width) {
        unsigned ch=(unsigned char)*s;if(ch<32||ch>126)ch='?';
        const uint8_t *glyph=font->pTable+(ch-32)*font->Sizes;
        for(unsigned row=0;row<font->Height;row++)for(unsigned col=0;col<font->Width;col++)
            if(glyph[row]&(1u<<col))pixel(x+(int)col,y+(int)row,color);
    }
}
static void line(int x0,int y0,int x1,int y1,uint16_t color)
{
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
    int dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,err=dx+dy;
    while(1) {
        pixel(x0,y0,color);if(x0==x1&&y0==y1)break;
        int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}
    }
}
static int cx(unsigned s){return GX+(int)(s%16)*CELL+6;}
static int cy(unsigned s){return GY+(int)(s/16)*CELL+6;}

static void tank(unsigned position,unsigned target,uint16_t color,int destroyed)
{
    int x=cx(position),y=cy(position);
    rect(x-5,y-5,2,11,RGB(75,91,110));rect(x+4,y-5,2,11,RGB(75,91,110));
    rect(x-3,y-4,7,9,color);rect(x-1,y-1,3,3,white);
    /* 炮塔图形与真正发射使用相同的 16 档方向，而不是只画四个主轴。 */
    int dx,dy;Battle_DirectionVector(Battle_AimDirection(position,target),&dx,&dy);
    line(x,y,x+dx*6/8,y+dy*6/8,white);
    if(destroyed) {
        line(x-5,y-5,x+5,y+5,gold);line(x-5,y+5,x+5,y-5,gold);
        rect(x-1,y-1,3,3,RGB(255,245,179));
    }
}

static void bullet(const BattleBullet *shot,uint16_t color)
{
    if(!shot->active)return;
    int x=GX+(int)(shot->x*CELL/16u),y=GY+(int)(shot->y*CELL/16u);
    /* 独立弹丸，不再连接发射者与目标。白色中心配合所属方颜色，反弹后
     * 用金色外沿，位置随逻辑飞行更新。旁边的小点数表示已撞墙次数。 */
    uint16_t edge=shot->collisions?gold:color;
    rect(x-2,y-1,5,3,edge);rect(x-1,y-2,3,5,edge);pixel(x,y,white);
    for(unsigned n=0;n<shot->collisions;n++)pixel(x-2+(int)n*3,y+4,gold);
}

static unsigned percent(unsigned wins,unsigned games){return games?wins*100u/games:0;}

void Tank_Display(uint32_t mode,uint32_t recent,const BattleScore *baseline,
                  const BattleScore *evaluation,const BattleScore *demo,TankDisplayStats *stats)
{
    uint32_t start=DWT->CYCCNT;
    for(unsigned n=0;n<240u*320u;n++)g_framebuffer[n]=bg;
    char label[48];
    text(12,3,"TANK BATTLE",white,&ASCII_Font16);
    text(174,5,"16 DIR",muted,&ASCII_Font12);
    text(12,22,"BLUE: RL",blue,&ASCII_Font12);text(156,22,"RED: FIX",red,&ASCII_Font12);
    if(mode==MODE_TRAIN)snprintf(label,sizeof(label),"TRAIN %5lu/%u EPS %lu%%",(unsigned long)g_agent.episodes,TANK_TRAIN_EPISODES,(unsigned long)g_agent.epsilon_permille/10);
    else if(mode==MODE_BASELINE)snprintf(label,sizeof(label),"RANDOM BASELINE  MAP %03lu",(unsigned long)g_world.map_id);
    else if(mode==MODE_EVALUATE)snprintf(label,sizeof(label),"EVALUATING 500 BATTLES");
    else snprintf(label,sizeof(label),"DEMO  BASE %u%% -> RL %u%%",percent(baseline->wins,baseline->games),percent(evaluation->wins,evaluation->games));
    text(12,39,label,mode==MODE_DEMO?green:muted,&ASCII_Font12);
    rect(12,56,216,3,RGB(35,51,70));
    unsigned progress=mode==MODE_TRAIN?g_agent.episodes*216u/TANK_TRAIN_EPISODES:mode==MODE_DEMO?216u:0u;
    rect(12,56,(int)progress,3,blue);
    for(unsigned s=0;s<256;s++) {
        int x=GX+(int)(s%16)*CELL,y=GY+(int)(s/16)*CELL;
        if(!g_world.open[s]) {
            rect(x+1,y+1,12,12,RGB(64,78,97));rect(x+2,y+2,10,2,RGB(84,99,118));
        } else rect(x+1,y+1,12,12,((s/16+s%16)&1)?RGB(19,31,46):RGB(22,35,50));
    }
    tank(g_game.blue,g_game.red,blue,(int)g_game.red_hit);
    tank(g_game.red,g_game.blue,red,(int)g_game.blue_hit);
    bullet(&g_game.blue_bullet,blue);bullet(&g_game.red_bullet,red);
    if(g_game.outcome==BATTLE_BLUE_WIN)text(12,280,"BLUE WINS",blue,&ASCII_Font16);
    else if(g_game.outcome==BATTLE_RED_WIN)text(12,280,"RED WINS",red,&ASCII_Font16);
    else if(g_game.outcome)text(12,280,g_game.outcome==BATTLE_TIMEOUT?"TIMEOUT":"BOTH HIT - DRAW",gold,&ASCII_Font16);
    else {
        snprintf(label,sizeof(label),"STEP %3lu  RED %s",(unsigned long)g_game.steps,g_game.phase==0?"READY":"AIMING");
        text(12,281,label,muted,&ASCII_Font12);
    }
    if(mode==MODE_DEMO)snprintf(label,sizeof(label),"PLAY W%lu L%lu D%lu B3",(unsigned long)demo->wins,(unsigned long)demo->losses,(unsigned long)demo->draws);
    else if(mode==MODE_TRAIN)snprintf(label,sizeof(label),"RECENT WIN %lu%%  Q 10 MiB",(unsigned long)recent);
    else snprintf(label,sizeof(label),"BASE WIN %u%%  RESET: LEARN",percent(baseline->wins,baseline->games));
    text(12,303,label,muted,&ASCII_Font12);
    /* SPI 由 CPU 发送。额外 clean 使 ST-LINK 截图也能读到真实完整帧。 */
    SCB_CleanDCache_by_Addr((uint32_t *)g_framebuffer,sizeof(g_framebuffer));
    if(LCD_CopyBuffer(0,0,240,320,g_framebuffer)==HAL_OK)stats->frames++;else stats->spi_errors++;
    stats->render_us=(DWT->CYCCNT-start)/(SystemCoreClock/1000000u);
}
