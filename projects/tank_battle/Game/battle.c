#include "battle.h"
#include <string.h>

/* 16 档方向，坐标以 1/16 格为单位；每子步约半格，无浮点累计漂移。
 * 速度方向约每 22.5° 一档（整数速度矢量存在少量量化误差）。 */
static const int shot_dx[16]={0,3,6,7,8,7,6,3,0,-3,-6,-7,-8,-7,-6,-3};
static const int shot_dy[16]={-8,-7,-6,-3,0,3,6,7,8,7,6,3,0,-3,-6,-7};

uint32_t Battle_AimDirection(uint32_t origin,uint32_t target)
{
    int dx=((int)(target%16)-(int)(origin%16))*16;
    int dy=((int)(target/16)-(int)(origin/16))*16;
    unsigned best=0;int best_dot=0,best_norm=1;
    for(unsigned d=0;d<16;d++) {
        int dot=dx*shot_dx[d]+dy*shot_dy[d];
        int norm=shot_dx[d]*shot_dx[d]+shot_dy[d]*shot_dy[d];
        if(dot>0&&(int64_t)dot*dot*best_norm>(int64_t)best_dot*best_dot*norm) {
            best=d;best_dot=dot;best_norm=norm;
        }
    }
    return best;
}

void Battle_DirectionVector(uint32_t direction,int *dx,int *dy)
{
    if(direction>=16)direction=0;
    *dx=shot_dx[direction];*dy=shot_dy[direction];
}

static int wall_point(const BattleWorld *w,int x,int y)
{
    return x<0||x>=256||y<0||y>=256||!w->open[(y/16)*16+x/16];
}

/* 线段与坦克轴对齐命中框的第一次交点参数。返回 2 表示不相交。
 * 用线段扫掠而不是只看终点，即使弹丸斜着掠过边角也不会漏检。 */
static float contact(int x0,int y0,int x1,int y1,unsigned tank)
{
    int origin[2]={x0,y0},delta[2]={x1-x0,y1-y0};
    int center[2]={(int)(tank%16)*16+8,(int)(tank/16)*16+8};
    float entry=0,exit=1;
    for(unsigned axis=0;axis<2;axis++) {
        int low=center[axis]-6,high=center[axis]+6;
        if(!delta[axis]) {if(origin[axis]<low||origin[axis]>high)return 2;}
        else {
            float a=(float)(low-origin[axis])/(float)delta[axis];
            float b=(float)(high-origin[axis])/(float)delta[axis];
            if(a>b){float tmp=a;a=b;b=tmp;}
            if(a>entry)entry=a;
            if(b<exit)exit=b;
            if(entry>exit)return 2;
        }
    }
    return entry;
}

static int direct_visible(const BattleWorld *w,unsigned source,unsigned target)
{
    int x=(int)(source%16)*16+8,y=(int)(source/16)*16+8;
    unsigned direction=Battle_AimDirection(source,target);
    int dx=shot_dx[direction],dy=shot_dy[direction];
    for(unsigned n=0;n<64;n++) {
        int nx=x+dx,ny=y+dy;
        if(wall_point(w,nx,y)||wall_point(w,x,ny)||wall_point(w,nx,ny))return 0;
        if(contact(x,y,nx,ny,target)<=1)return 1;
        x=nx;y=ny;
    }
    return 0;
}


uint32_t Battle_Random(uint32_t *rng)
{
    uint32_t x=*rng?*rng:1u;x^=x<<13;x^=x>>17;x^=x<<5;return *rng=x;
}

int Battle_WorldInit(BattleWorld *w,const char cells[16][17],uint32_t map_id)
{
    memset(w,0,sizeof(*w));memset(w->distance,0xff,sizeof(w->distance));w->map_id=map_id;
    for(unsigned s=0;s<256;s++) {
        if(cells[s/16][s%16]=='.') {w->open[s]=1;w->free_cells[w->free_count++]=(uint16_t)s;}
        else if(cells[s/16][s%16]!='#')return 0;
    }
    if(w->free_count<8)return 0;
    const int dr[4]={-1,0,1,0},dc[4]={0,1,0,-1};
    for(unsigned s=0;s<256;s++)for(unsigned a=0;a<4;a++) {
        int r=(int)(s/16)+dr[a],c=(int)(s%16)+dc[a];
        w->neighbor[s][a]=(uint16_t)s;
        if(w->open[s]&&r>=0&&r<16&&c>=0&&c<16&&w->open[r*16+c])w->neighbor[s][a]=(uint16_t)(r*16+c);
    }
    /* 对每个可走格做一次 BFS。红方只会沿最短距离靠近；蓝方不使用这个表
     * 决定动作。蓝方的行为全部由在线学习出来的 Q 表和 ε-greedy 决定。 */
    for(unsigned i=0;i<w->free_count;i++) {
        unsigned source=w->free_cells[i],head=0,tail=0;uint16_t queue[256];
        queue[tail++]=(uint16_t)source;w->distance[source][source]=0;
        while(head<tail) {
            unsigned pos=queue[head++];
            for(unsigned a=0;a<4;a++) {
                unsigned next=w->neighbor[pos][a];
                if(w->distance[source][next]!=BATTLE_UNREACHABLE)continue;
                w->distance[source][next]=(uint16_t)(w->distance[source][pos]+1);queue[tail++]=(uint16_t)next;
            }
        }
        if(tail!=w->free_count)return 0;
        for(unsigned j=0;j<w->free_count;j++) {
            unsigned target=w->free_cells[j];if(source==target)continue;
            w->visible[source][target]=(uint8_t)direct_visible(w,source,target);
        }
    }
    return 1;
}

void Battle_Reset(const BattleWorld *w,BattleState *s,uint32_t *rng)
{
    memset(s,0,sizeof(*s));
    s->blue=w->free_cells[Battle_Random(rng)%w->free_count];
    /* 至少间隔 4 格，避免开局双方挤在一起。地图已经验证全路格连通。 */
    unsigned farthest=0;
    for(unsigned i=0;i<w->free_count;i++)if(w->distance[s->blue][w->free_cells[i]]>farthest)farthest=w->distance[s->blue][w->free_cells[i]];
    unsigned minimum=farthest<4?farthest:4;
    do {s->red=w->free_cells[Battle_Random(rng)%w->free_count];}
    while(s->red==s->blue||w->distance[s->blue][s->red]<minimum);
    s->phase=Battle_Random(rng)%BATTLE_PHASES;
    s->blue_action=ACTION_WAIT;s->red_action=ACTION_WAIT;
}

uint32_t Battle_RedAction(const BattleWorld *w,const BattleState *s)
{
    /* 红方固定规则：每 4 步有一个开火窗口；能直视且窗口打开就射击。
     * 其它时候寻路接近。平局固定按上、右、下、左优先，训练期间不改变。
     * 红方没有学习或动态升级，phase 已明确加入蓝方状态，避免隐藏时钟。 */
    if(s->phase==0&&!s->red_bullet.active&&w->visible[s->red][s->blue])return ACTION_FIRE;
    unsigned best=ACTION_WAIT;uint16_t distance=w->distance[s->red][s->blue];
    for(unsigned a=0;a<4;a++) {
        unsigned next=w->neighbor[s->red][a];
        if(next==s->blue)continue;
        if(w->distance[next][s->blue]<distance) {distance=w->distance[next][s->blue];best=a;}
    }
    return best;
}

static unsigned direction_from_vector(int dx,int dy)
{
    for(unsigned d=0;d<16;d++)if(shot_dx[d]==dx&&shot_dy[d]==dy)return d;
    return 0;
}

static void fire(BattleBullet *bullet,unsigned origin,unsigned target)
{
    bullet->active=1;bullet->position=origin;bullet->collisions=0;
    bullet->x=(origin%16)*16+8;bullet->y=(origin/16)*16+8;
    bullet->direction=Battle_AimDirection(origin,target);
}

static void advance(const BattleWorld *w,BattleBullet *bullet)
{
    if(!bullet->active)return;
    int x=(int)bullet->x,y=(int)bullet->y;
    int dx=shot_dx[bullet->direction],dy=shot_dy[bullet->direction];
    int block_x=wall_point(w,x+dx,y),block_y=wall_point(w,x,y+dy);
    if(block_x||block_y||wall_point(w,x+dx,y+dy)) {
        /* 依墙面法线翻转速度分量；孤立角点或双轴碰撞翻转两轴。
         * 同一子步的角点仅计 1 次，第三次墙碰直接令 active=0。 */
        if(block_x)dx=-dx;
        if(block_y)dy=-dy;
        if(!block_x&&!block_y){dx=-dx;dy=-dy;}
        bullet->collisions++;
        if(bullet->collisions>=BATTLE_BULLET_COLLISIONS){bullet->active=0;return;}
        bullet->direction=direction_from_vector(dx,dy);
    } else {
        bullet->x=(unsigned)(x+dx);bullet->y=(unsigned)(y+dy);
        bullet->position=(bullet->y/16)*16+bullet->x/16;
    }
}

static void hit(BattleState *s,BattleBullet *bullet,int x0,int y0,int blue_owner)
{
    if(!bullet->active)return;
    float blue_contact=contact(x0,y0,(int)bullet->x,(int)bullet->y,s->blue);
    float red_contact=contact(x0,y0,(int)bullet->x,(int)bullet->y,s->red);
    /* 未反弹的自弹有炮口保护；第一次反弹后可伤到发射者。避免子弹出生在
     * 炮塔中心时立刻自伤，不需要额外隐藏的“已离开炮口”状态。 */
    if(!bullet->collisions){if(blue_owner)blue_contact=2;else red_contact=2;}
    if(blue_contact<=1&&blue_contact<=red_contact){s->red_hit=1;bullet->active=0;}
    else if(red_contact<=1){s->blue_hit=1;bullet->active=0;}
}

BattleTransition Battle_StepActions(const BattleWorld *w,BattleState *s,uint32_t ba,uint32_t ra)
{
    BattleTransition t={0,0,0};
    if(s->outcome!=BATTLE_RUNNING)return t;
    if(ba>=BATTLE_ACTIONS)ba=ACTION_WAIT;
    if(ra>=BATTLE_ACTIONS)ra=ACTION_WAIT;
    unsigned old_blue=s->blue,old_red=s->red;
    unsigned blue=ba<4?w->neighbor[old_blue][ba]:old_blue;
    unsigned red=ra<4?w->neighbor[old_red][ra]:old_red;
    s->blocked=(ba<4&&blue==old_blue);s->blue_hit=0;s->red_hit=0;
    if(blue==red||(blue==old_red&&red==old_blue)){blue=old_blue;red=old_red;s->blocked=ba<4;}
    s->blue=blue;s->red=red;s->blue_action=ba;s->red_action=ra;
    /* 坦克先同步移动；走进已经存在的子弹会被击中。 */
    hit(s,&s->blue_bullet,(int)s->blue_bullet.x,(int)s->blue_bullet.y,1);
    hit(s,&s->red_bullet,(int)s->red_bullet.x,(int)s->red_bullet.y,0);
    unsigned fired=0;
    if(!s->blue_hit&&!s->red_hit) {
        /* 每方最多一颗在飞子弹，必须等它消失才可再次发射。 */
        if(ba==ACTION_FIRE&&!s->blue_bullet.active){fire(&s->blue_bullet,blue,red);fired=1;}
        if(ra==ACTION_FIRE&&!s->red_bullet.active)fire(&s->red_bullet,red,blue);
        for(unsigned sub=0;sub<BATTLE_BULLET_SUBSTEPS;sub++) {
            int bx=(int)s->blue_bullet.x,by=(int)s->blue_bullet.y;
            int rx=(int)s->red_bullet.x,ry=(int)s->red_bullet.y;
            advance(w,&s->blue_bullet);advance(w,&s->red_bullet);
            hit(s,&s->blue_bullet,bx,by,1);hit(s,&s->red_bullet,rx,ry,0);
            /* 该子步双方都完成碰撞检查后才结束，允许同时命中判平。 */
            if(s->blue_hit||s->red_hit)break;
        }
    }
    s->steps++;s->phase=(s->phase+1)%BATTLE_PHASES;
    t.reward=-0.02f-(s->blocked?0.15f:0.0f)-((ba==ACTION_FIRE&&!fired)?0.05f:0.0f);
    if(s->blue_hit&&s->red_hit){s->outcome=BATTLE_MUTUAL;t.reward-=4.0f;}
    else if(s->blue_hit){s->outcome=BATTLE_BLUE_WIN;t.reward+=20.0f;}
    else if(s->red_hit){s->outcome=BATTLE_RED_WIN;t.reward-=20.0f;}
    t.terminated=s->outcome!=BATTLE_RUNNING;
    float old_phi=-0.05f*(float)w->distance[old_blue][old_red];
    float next_phi=t.terminated?0.0f:-0.05f*(float)w->distance[blue][red];
    t.reward+=BATTLE_GAMMA*next_phi-old_phi;
    if(!t.terminated&&s->steps>=BATTLE_MAX_STEPS){s->outcome=BATTLE_TIMEOUT;t.truncated=1;}
    return t;
}

BattleTransition Battle_Step(const BattleWorld *w,BattleState *s,uint32_t action)
{
    return Battle_StepActions(w,s,action,Battle_RedAction(w,s));
}

static uint64_t bullet_key(const BattleBullet *b)
{
    if(!b->active)return 0;
    return 1u|((uint64_t)b->x<<1)|((uint64_t)b->y<<9)|((uint64_t)b->direction<<17)|((uint64_t)b->collisions<<21);
}

uint64_t Battle_StateKey(const BattleState *s)
{
    uint64_t key=s->blue|((uint64_t)s->red<<8)|((uint64_t)s->phase<<16);
    key|=bullet_key(&s->blue_bullet)<<18;
    key|=bullet_key(&s->red_bullet)<<41;
    return key+1u;
}
