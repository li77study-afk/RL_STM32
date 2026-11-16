/** 宽阔掩体竞技场：车弹等速、0.5 秒独立冷却、多弹丸、反弹射击解。 */
#include "battle.h"
#include "fixed_policy.h"
#include <string.h>

/* 每子步长度为 4，四个子步总长度为 16，等于坦克每拍移动距离。
 * 16 档方向使用归一化矢量，斜向不会因为同时增加 x/y 而变快。 */
static const float vx[16]={0,1.53073373f,2.82842712f,3.69551813f,4,3.69551813f,2.82842712f,1.53073373f,0,-1.53073373f,-2.82842712f,-3.69551813f,-4,-3.69551813f,-2.82842712f,-1.53073373f};
static const float vy[16]={-4,-3.69551813f,-2.82842712f,-1.53073373f,0,1.53073373f,2.82842712f,3.69551813f,4,3.69551813f,2.82842712f,1.53073373f,0,-1.53073373f,-2.82842712f,-3.69551813f};
static float center_x(unsigned s){return (float)(s%16)*16+8;}
static float center_y(unsigned s){return (float)(s/16)*16+8;}

uint32_t Battle_AimDirection(uint32_t origin,uint32_t target)
{
    float dx=center_x(target)-center_x(origin),dy=center_y(target)-center_y(origin),best_dot=-1e30f;
    unsigned best=0;
    for(unsigned d=0;d<16;d++){float dot=dx*vx[d]+dy*vy[d];if(dot>best_dot){best_dot=dot;best=d;}}
    return best;
}
void Battle_DirectionVector(uint32_t d,int *dx,int *dy)
{
    if(d>=16)d=0;
    float x=vx[d]*2,y=vy[d]*2;
    *dx=(int)(x+(x>=0?0.5f:-0.5f));*dy=(int)(y+(y>=0?0.5f:-0.5f));
}
static int wall_point(const BattleWorld *w,float x,float y)
{
    return x<0||x>=256||y<0||y>=256||!w->open[((unsigned)y/16)*16+(unsigned)x/16];
}
static float contact(float x0,float y0,float x1,float y1,unsigned tank)
{
    float cx=center_x(tank),cy=center_y(tank);
    /* 大部分弹丸远离候选位置，先用包围框快速排除，避免重复除法。 */
    if((x0<cx-6&&x1<cx-6)||(x0>cx+6&&x1>cx+6)||
       (y0<cy-6&&y1<cy-6)||(y0>cy+6&&y1>cy+6))return 2;
    float origin[2]={x0,y0},delta[2]={x1-x0,y1-y0},c[2]={cx,cy},entry=0,exit=1;
    for(unsigned axis=0;axis<2;axis++) {
        float low=c[axis]-6,high=c[axis]+6;
        if(delta[axis]==0){if(origin[axis]<low||origin[axis]>high)return 2;}
        else {
            float a=(low-origin[axis])/delta[axis],b=(high-origin[axis])/delta[axis];
            if(a>b){float tmp=a;a=b;b=tmp;}
            if(a>entry)entry=a;
            if(b<exit)exit=b;
            if(entry>exit)return 2;
        }
    }
    return entry;
}
static void spawn(BattleBullet *b,unsigned origin,unsigned direction)
{
    memset(b,0,sizeof(*b));b->active=1;b->position=origin;b->x=center_x(origin);b->y=center_y(origin);b->direction=direction;
}
static void advance(const BattleWorld *w,BattleBullet *b)
{
    if(!b->active)return;
    float dx=vx[b->direction],dy=vy[b->direction];
    int bx=wall_point(w,b->x+dx,b->y),by=wall_point(w,b->x,b->y+dy);
    if(bx||by||wall_point(w,b->x+dx,b->y+dy)) {
        unsigned direction=b->direction;
        if(!bx&&!by)direction=(direction+8)%16;
        else {if(bx)direction=(16-direction)%16;if(by)direction=(8+16-direction)%16;}
        b->collisions++;
        if(b->collisions>=BATTLE_BULLET_COLLISIONS){b->active=0;return;}
        b->direction=direction;
    } else {
        b->x+=dx;b->y+=dy;b->position=((unsigned)b->y/16)*16+(unsigned)b->x/16;
    }
}
uint32_t Battle_Random(uint32_t *rng)
{
    uint32_t x=*rng?*rng:1u;x^=x<<13;x^=x>>17;x^=x<<5;return *rng=x;
}
uint32_t Battle_ActiveBullets(const BattleBullet *bullets)
{
    unsigned n=0;for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++)n+=bullets[i].active!=0;return n;
}

int Battle_WorldInit(BattleWorld *w,const char cells[16][17],uint32_t id)
{
    memset(w,0,sizeof(*w));memset(w->distance,0xff,sizeof(w->distance));memset(w->shot_time,255,sizeof(w->shot_time));w->map_id=id;
    for(unsigned s=0;s<256;s++) {
        if(cells[s/16][s%16]=='.'){w->open[s]=1;w->free_cells[w->free_count++]=(uint16_t)s;}
        else if(cells[s/16][s%16]!='#')return 0;
    }
    if(w->free_count<8)return 0;
    const int dr[4]={-1,0,1,0},dc[4]={0,1,0,-1};
    for(unsigned s=0;s<256;s++)for(unsigned a=0;a<4;a++) {
        int r=(int)(s/16)+dr[a],c=(int)(s%16)+dc[a];w->neighbor[s][a]=(uint16_t)s;
        if(w->open[s]&&r>=0&&r<16&&c>=0&&c<16&&w->open[r*16+c])w->neighbor[s][a]=(uint16_t)(r*16+c);
    }
    for(unsigned i=0;i<w->free_count;i++) {
        unsigned source=w->free_cells[i],head=0,tail=0;uint16_t queue[256];queue[tail++]=(uint16_t)source;w->distance[source][source]=0;
        while(head<tail){unsigned p=queue[head++];for(unsigned a=0;a<4;a++){
            unsigned n=w->neighbor[p][a];if(w->distance[source][n]!=BATTLE_UNREACHABLE)continue;
            w->distance[source][n]=(uint16_t)(w->distance[source][p]+1);queue[tail++]=(uint16_t)n;
        }}
        if(tail!=w->free_count)return 0;
        /* 对每个炮塔方向追踪到第三次墙碰，并为经过的可行走格记录射击解。
         * 优先短路径、少反弹；若会先反弹打中发射者就停止该射线。双方共用。 */
        for(unsigned direction=0;direction<16;direction++) {
            BattleBullet b;spawn(&b,source,direction);
            for(unsigned step=1;step<=220;step++) {
                float ox=b.x,oy=b.y;advance(w,&b);if(!b.active)break;
                if(b.collisions&&contact(ox,oy,b.x,b.y,source)<=1)break;
                float minx=ox<b.x?ox:b.x,maxx=ox>b.x?ox:b.x,miny=oy<b.y?oy:b.y,maxy=oy>b.y?oy:b.y;
                int x0=(int)((minx-6)/16),x1=(int)((maxx+6)/16),y0=(int)((miny-6)/16),y1=(int)((maxy+6)/16);
                for(int r=y0;r<=y1;r++)for(int c=x0;c<=x1;c++) {
                    if(r<0||r>=16||c<0||c>=16)continue;
                    unsigned target=(unsigned)r*16+(unsigned)c;if(target==source||!w->open[target])continue;
                    unsigned cost=step+b.collisions*12;
                    if(cost<255&&cost<w->shot_time[source][target]&&contact(ox,oy,b.x,b.y,target)<=1){
                        w->shot_time[source][target]=(uint8_t)cost;w->shot_plan[source][target]=(uint8_t)(direction+1+(b.collisions<<5));
                    }
                }
            }
        }
    }
    return 1;
}

/* 对 5 个候选移动落点预测近两拍弹道危险。这是观测特征，不直接替蓝方选动作。
 * 敌弹和反弹自弹都会进入预测；同一方未反弹子弹有炮口保护。 */
void Battle_PredictThreats(const BattleWorld *w,const BattleState *s,BattleTeam team,uint32_t predict_reflections,uint32_t *now,uint32_t *later)
{
    int blue=team==BATTLE_TEAM_BLUE;
    unsigned own=blue?s->blue:s->red,enemy=blue?s->red:s->blue;*now=0;*later=0;
    unsigned candidate[5];for(unsigned a=0;a<5;a++){candidate[a]=a<4?w->neighbor[own][a]:own;if(candidate[a]==enemy)candidate[a]=own;}
    for(unsigned team=0;team<2;team++) {
        const BattleBullet *pool=team?s->red_bullets:s->blue_bullets;
        int own_shot=(blue&&!team)||(!blue&&team);
        for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++)if(pool[i].active) {
            BattleBullet b=pool[i];
            if(!own_shot||b.collisions)for(unsigned a=0;a<5;a++)if(contact(b.x,b.y,b.x,b.y,candidate[a])<=1)*now|=1u<<a;
            for(unsigned k=0;k<2*BATTLE_BULLET_SUBSTEPS;k++) {
                float ox=b.x,oy=b.y;unsigned old_bounces=b.collisions;advance(w,&b);if(!b.active)break;
                /* 调用方决定是否预测尚未发生的墙碰，物理工具不绑定某种对手策略。 */
                if(!predict_reflections&&b.collisions!=old_bounces)break;
                if(own_shot&&!b.collisions)continue;
                for(unsigned a=0;a<5;a++)if(contact(ox,oy,b.x,b.y,candidate[a])<=1){if(k<BATTLE_BULLET_SUBSTEPS)*now|=1u<<a;else *later|=1u<<a;}
            }
        }
    }
}
void Battle_Observe(const BattleWorld *w,BattleState *s)
{
    unsigned walls=0,approach=0,dist=w->distance[s->blue][s->red];
    uint32_t now,later;
    for(unsigned a=0;a<4;a++){
        unsigned next=w->neighbor[s->blue][a];
        if(next==s->blue||next==s->red)walls|=1u<<a;
        if(w->distance[next][s->red]<dist)approach|=1u<<a;
    }
    Battle_PredictThreats(w,s,BATTLE_TEAM_BLUE,1,&now,&later);
    unsigned distance_bin=dist<3?0:dist<6?1:dist<10?2:3;
    unsigned plan=w->shot_plan[s->blue][s->red],kind=plan?(plan>>5)+1:0;
    unsigned own_count=Battle_ActiveBullets(s->blue_bullets),enemy_count=Battle_ActiveBullets(s->red_bullets);
    if(own_count>3)own_count=3;
    if(enemy_count>3)enemy_count=3;
    /* 聚合后的战术观测：掩体/路径方向、相对方位和距离、射击解、冷却、
     * 两拍风险和弹幕密度。它不是完整物理状态，不能宣称精确 Markov Q 表。 */
    uint64_t key=walls|((uint64_t)approach<<4)|((uint64_t)Battle_AimDirection(s->blue,s->red)<<8)|
        ((uint64_t)distance_bin<<12)|((uint64_t)kind<<14)|((uint64_t)(s->blue_cooldown<=1)<<16)|
        ((uint64_t)now<<17)|((uint64_t)later<<22)|((uint64_t)own_count<<27)|((uint64_t)enemy_count<<29)|
        ((uint64_t)(s->red_cooldown<=1)<<31);
    s->observation=key+1u;
}
uint64_t Battle_StateKey(const BattleState *s){return s->observation;}
void Battle_Reset(const BattleWorld *w,BattleState *s,uint32_t *rng)
{
    memset(s,0,sizeof(*s));s->blue=w->free_cells[Battle_Random(rng)%w->free_count];
    do{s->red=w->free_cells[Battle_Random(rng)%w->free_count];}while(s->red==s->blue||w->distance[s->blue][s->red]<BATTLE_MIN_SPAWN_DISTANCE);
    s->blue_action=ACTION_WAIT;s->red_action=ACTION_WAIT;Battle_Observe(w,s);
}
uint32_t Battle_RedAction(const BattleWorld *w,const BattleState *s)
{
    const FixedPolicyInput input={w,s,POLICY_TRAIN};
    return FixedPolicy_Decide(&input);
}
static int fire_pool(const BattleWorld *w,BattleBullet *pool,unsigned source,unsigned target)
{
    unsigned plan=w->shot_plan[source][target],direction=plan?((plan&31u)-1u):Battle_AimDirection(source,target);
    for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++)if(!pool[i].active){spawn(pool+i,source,direction);return 1;}
    return 0;
}
static void hit(BattleState *s,BattleBullet *b,float ox,float oy,int blue_owner)
{
    if(!b->active)return;
    float a=contact(ox,oy,b->x,b->y,s->blue),c=contact(ox,oy,b->x,b->y,s->red);
    if(!b->collisions){if(blue_owner)a=2;else c=2;}
    if(a<=1&&a<=c){s->red_hit=1;if(!blue_owner&&b->collisions)s->red_bank_kills++;b->active=0;}
    else if(c<=1){s->blue_hit=1;if(blue_owner&&b->collisions)s->blue_bank_kills++;b->active=0;}
}
BattleTransition Battle_StepActions(const BattleWorld *w,BattleState *s,uint32_t ba,uint32_t ra)
{
    BattleTransition t={0,0,0};if(s->outcome)return t;
    if(ba>=10)ba=ACTION_WAIT;
    if(ra>=10)ra=ACTION_WAIT;
    unsigned bm=ba%5,rm=ra%5,old_blue=s->blue,old_red=s->red;
    unsigned blue=bm<4?w->neighbor[old_blue][bm]:old_blue,red=rm<4?w->neighbor[old_red][rm]:old_red;
    s->blocked=bm<4&&blue==old_blue;s->blue_hit=0;s->red_hit=0;
    if(blue==red||(blue==old_red&&red==old_blue)){blue=old_blue;red=old_red;s->blocked=bm<4;}
    s->blue=blue;s->red=red;s->blue_action=ba;s->red_action=ra;
    if(s->blue_cooldown)s->blue_cooldown--;
    if(s->red_cooldown)s->red_cooldown--;
    s->blue_ready+=s->blue_cooldown==0;s->red_ready+=s->red_cooldown==0;
    for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++){
        hit(s,s->blue_bullets+i,s->blue_bullets[i].x,s->blue_bullets[i].y,1);
        hit(s,s->red_bullets+i,s->red_bullets[i].x,s->red_bullets[i].y,0);
    }
    unsigned fired=0;
    if(!s->blue_hit&&!s->red_hit) {
        if(ba>=5&&!s->blue_cooldown&&fire_pool(w,s->blue_bullets,blue,red)){
            s->blue_cooldown=BATTLE_FIRE_COOLDOWN;s->blue_shots++;fired=1;
        }
        if(ra>=5&&!s->red_cooldown&&fire_pool(w,s->red_bullets,red,blue)){
            s->red_cooldown=BATTLE_FIRE_COOLDOWN;s->red_shots++;
        }
        for(unsigned sub=0;sub<BATTLE_BULLET_SUBSTEPS;sub++) {
            for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++) {
                BattleBullet *b=s->blue_bullets+i,*r=s->red_bullets+i;
                float bx=b->x,by=b->y,rx=r->x,ry=r->y;
                unsigned before_b=b->collisions,before_r=r->collisions;
                advance(w,b);advance(w,r);s->wall_bounces+=b->collisions-before_b+r->collisions-before_r;
                hit(s,b,bx,by,1);hit(s,r,rx,ry,0);
            }
            if(s->blue_hit||s->red_hit)break;
        }
    }
    s->steps++;s->phase=s->steps%BATTLE_PHASES;
    /* 实际发射给进攻节奏奖励，就绪却一直不射击有小代价。奖励上限
     * 0.1/5=0.02 小于每拍代价 0.03，不能靠原地刷开火获得正收益。 */
    t.reward=-0.03f-(s->blocked?0.1f:0.0f)+(fired?0.1f:0.0f)
        -(!fired&&!s->blue_cooldown?0.15f:0.0f);
    if(s->blue_hit&&s->red_hit){s->outcome=BATTLE_MUTUAL;t.reward-=6;}
    else if(s->blue_hit){s->outcome=BATTLE_BLUE_WIN;t.reward+=25;}
    else if(s->red_hit){s->outcome=BATTLE_RED_WIN;t.reward-=25;}
    t.terminated=s->outcome!=BATTLE_RUNNING;
    float old_phi=-0.03f*w->distance[old_blue][old_red],next_phi=t.terminated?0:-0.03f*w->distance[blue][red];
    t.reward+=BATTLE_GAMMA*next_phi-old_phi;
    if(!t.terminated&&s->steps>=BATTLE_MAX_STEPS){s->outcome=BATTLE_TIMEOUT;t.truncated=1;}
    Battle_Observe(w,s);return t;
}
BattleTransition Battle_Step(const BattleWorld *w,BattleState *s,uint32_t action)
{
    return Battle_StepWithPolicy(w,s,action,POLICY_TRAIN);
}

BattleTransition Battle_StepWithPolicy(const BattleWorld *w,BattleState *s,uint32_t action,BattlePolicyStage stage)
{
    /* 先让固定红方读取旧状态决策，再统一结算双方动作。 */
    const FixedPolicyInput input={w,s,stage};
    uint32_t red_action=FixedPolicy_Decide(&input);
    return Battle_StepActions(w,s,action,red_action);
}
