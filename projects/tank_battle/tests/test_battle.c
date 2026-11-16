/** 规则独立测试 + 多种子“训练前随机基线 / 训练后贪心策略”比较。 */
#include "battle.h"
#include "tank_agent.h"
#include "map_library.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static BattleWorld world;
static TankQEntry table[TANK_Q_CAPACITY];

static BattleState setup(unsigned blue,unsigned red,unsigned phase)
{
    BattleState s={0};s.blue=blue;s.red=red;s.phase=phase;return s;
}

static BattleBullet shot(unsigned cell,unsigned direction,unsigned collisions)
{
    BattleBullet b={0};b.active=1;b.position=cell;b.direction=direction;b.collisions=collisions;
    b.x=(cell%16)*16+8;b.y=(cell/16)*16+8;return b;
}

static void rules(void)
{
    char map[16][17];for(unsigned r=0;r<16;r++){memset(map[r],'.',16);map[r][16]=0;}
    assert(Battle_WorldInit(&world,map,0));assert(world.free_count==256);
    assert(world.distance[0][255]==30&&world.visible[17][21]&&world.visible[17][34]);
    BattleState s=setup(17,21,0);assert(Battle_RedAction(&world,&s)==ACTION_FIRE);
    s.phase=1;assert(Battle_RedAction(&world,&s)==ACTION_LEFT);
    /* 距离 4 格时，第一拍飞到中间，不会变成瞬时激光命中。 */
    s=setup(17,21,0);BattleTransition t=Battle_StepActions(&world,&s,ACTION_FIRE,ACTION_WAIT);
    assert(!t.terminated&&s.outcome==BATTLE_RUNNING&&s.blue_bullet.active&&s.blue_bullet.position==19);
    t=Battle_StepActions(&world,&s,ACTION_FIRE,ACTION_WAIT);assert(s.outcome==BATTLE_BLUE_WIN&&t.terminated);
    s=setup(17,21,0);Battle_StepActions(&world,&s,ACTION_FIRE,ACTION_FIRE);
    assert(s.outcome==BATTLE_RUNNING);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);assert(s.outcome==BATTLE_MUTUAL);
    /* 先看见来弹，再侧移；子弹仍沿原方向，不追踪移动的目标。 */
    s=setup(17,21,0);Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_FIRE);
    Battle_StepActions(&world,&s,ACTION_DOWN,ACTION_WAIT);assert(s.blue==33&&!s.red_hit&&s.outcome==BATTLE_RUNNING);
    s=setup(17,19,0);Battle_StepActions(&world,&s,ACTION_RIGHT,ACTION_LEFT);assert(s.blue==17&&s.red==19);
    s=setup(17,18,0);Battle_StepActions(&world,&s,ACTION_RIGHT,ACTION_LEFT);assert(s.blue==17&&s.red==18);
    s=setup(0,255,0);Battle_StepActions(&world,&s,ACTION_UP,ACTION_WAIT);assert(s.blue==0&&s.blocked);
    s=setup(17,34,0);s.steps=BATTLE_MAX_STEPS-1;
    t=Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);assert(s.outcome==BATTLE_TIMEOUT&&!t.terminated&&t.truncated);
    BattleState ended=s;t=Battle_StepActions(&world,&s,ACTION_FIRE,ACTION_FIRE);assert(memcmp(&s,&ended,sizeof(s))==0&&t.reward==0);
    /* 单格夹道：前两次撞墙反弹仍存在，第 3 次碰撞消失。 */
    map[1][1]='#';map[1][3]='#';assert(Battle_WorldInit(&world,map,0));
    s=setup(64,255,0);s.blue_bullet=shot(18,4,0);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(s.blue_bullet.active&&s.blue_bullet.collisions==2&&s.blue_bullet.position==18);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(!s.blue_bullet.active&&s.blue_bullet.collisions==3);
    /* 斜向撞垂直墙只翻转水平分量；孤立角点同时翻转两个分量。 */
    map[4][5]='#';assert(Battle_WorldInit(&world,map,0));
    s=setup(0,255,0);s.blue_bullet=shot(68,6,0);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(s.blue_bullet.active&&s.blue_bullet.direction==10&&s.blue_bullet.collisions==1&&s.blue_bullet.position==84);
    map[4][5]='.';map[5][5]='#';assert(Battle_WorldInit(&world,map,0));
    s=setup(0,255,0);s.blue_bullet=shot(68,6,0);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(s.blue_bullet.active&&s.blue_bullet.direction==14&&s.blue_bullet.collisions==1&&s.blue_bullet.x==66&&s.blue_bullet.y==66);
    map[5][5]='.';assert(Battle_WorldInit(&world,map,0));
    /* 第 3 次碰撞不是第 3 个时间步。没有撞墙的长直线飞行不能提前消失。 */
    s=setup(64,255,0);s.blue_bullet=shot(80,4,0);
    for(unsigned n=0;n<3;n++)Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(s.blue_bullet.active&&s.blue_bullet.collisions==0&&s.blue_bullet.position==86);
    /* 非 45° 斜向飞行保留细分坐标，不能退化为只走格子对角线。 */
    s=setup(64,255,0);s.blue_bullet=shot(102,1,0);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(s.blue_bullet.direction==1&&s.blue_bullet.x==116&&s.blue_bullet.y==76);
    /* 反弹回来的自弹也会伤到自己。 */
    s=setup(17+16,21+16,0);s.blue_bullet=shot(34,12,1);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);assert(s.outcome==BATTLE_RED_WIN);
    /* 每颗子弹每个子步都检查坦克，速度为 2 格也不能跳过中间目标。 */
    s=setup(64,82,0);s.blue_bullet=shot(81,4,0);
    Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);assert(s.outcome==BATTLE_BLUE_WIN);
    /* 完整状态键包含方向/碰撞计数/双方子弹，忽略不活动子弹的残留字段。 */
    s=setup(64,255,0);uint64_t key=Battle_StateKey(&s);s.blue_bullet.position=80;
    assert(Battle_StateKey(&s)==key);s.blue_bullet.active=1;uint64_t flight=Battle_StateKey(&s);
    assert(flight!=key);s.blue_bullet.x=1;assert(Battle_StateKey(&s)!=flight);s.blue_bullet.x=0;s.blue_bullet.collisions=1;assert(Battle_StateKey(&s)!=flight);
    s.blue_bullet.collisions=0;s.blue_bullet.direction=1;assert(Battle_StateKey(&s)!=flight);
    TankAgent a;TankAgent_Init(&a,table,1);BattleState before=setup(17,21,0),after=setup(17,21,1);
    t=(BattleTransition){10,1,0};TankAgent_Update(&a,&before,ACTION_FIRE,&after,t);
    assert(fabsf(TankAgent_Q(&a,&before,ACTION_FIRE)-1.8f)<0.0001f);
    TankAgent_Init(&a,table,1);
    t=(BattleTransition){10.0f/TANK_ALPHA,1,0};TankAgent_Update(&a,&after,ACTION_UP,&after,t);
    t=(BattleTransition){-1,0,1};TankAgent_Update(&a,&before,ACTION_FIRE,&after,t);
    assert(fabsf(TankAgent_Q(&a,&before,ACTION_FIRE)-1.548f)<0.0001f);
    memset(map[8],'#',16);assert(!Battle_WorldInit(&world,map,0));
    puts("PASS rules: real flight, fixed direction, 3-wall limit, ricochet self-hit, no tunneling, synchronous moves, TD, full state keys.");
}

static uint32_t q_hash(void)
{
    uint32_t h=2166136261u;const unsigned char *bytes=(const unsigned char *)table;
    for(unsigned n=0;n<sizeof(table);n++)h=(h^bytes[n])*16777619u;
    return h;
}

int main(void)
{
    rules();assert(Map_Init(75));assert(Battle_WorldInit(&world,g_current_map.cells,75));
    assert(sizeof(table)==10u*1024u*1024u);
    const uint32_t seeds[3]={750,1,42};
    for(unsigned run=0;run<3;run++) {
        TankAgent a;TankAgent_Init(&a,table,seeds[run]);uint32_t spawn_rng=seeds[run]^0x51a750u;
        BattleScore baseline=TankAgent_Evaluate(&world,&a,0xbaf123u,1000,1);
        printf("BASE seed=%u W=%u L=%u D=%u timeout=%u\n",seeds[run],baseline.wins,baseline.losses,baseline.draws,baseline.timeouts);
        while(a.episodes<TANK_TRAIN_EPISODES) {
            BattleState s;Battle_Reset(&world,&s,&spawn_rng);
            while(s.outcome==BATTLE_RUNNING) {
                BattleState before=s;unsigned action=TankAgent_Choose(&a,&s);
                BattleTransition t=Battle_Step(&world,&s,action);TankAgent_Update(&a,&before,action,&s,t);
                assert(world.open[s.blue]&&world.open[s.red]&&s.blue!=s.red&&s.phase<4);
                const BattleBullet *shots[2]={&s.blue_bullet,&s.red_bullet};
                for(unsigned k=0;k<2;k++)if(shots[k]->active)
                    assert(shots[k]->position<256&&world.open[shots[k]->position]&&shots[k]->direction<16&&shots[k]->x<256&&shots[k]->y<256&&shots[k]->collisions<3);
            }
            TankAgent_Finish(&a,&s);
            if(a.episodes%10000==0) {
                BattleScore progress=TankAgent_Evaluate(&world,&a,0x7155u,300,0);
                printf("TRAIN seed=%u ep=%u updates=%u eps=%u evalW=%u/300 L=%u D=%u\n",seeds[run],a.episodes,a.updates,a.epsilon_permille,progress.wins,progress.losses,progress.draws);fflush(stdout);
            }
        }
        TankAgent saved=a;uint32_t hash=q_hash();
        BattleScore final=TankAgent_Evaluate(&world,&a,0xbaf123u,1000,0);
        assert(hash==q_hash()&&memcmp(&saved,&a,sizeof(a))==0);
        for(unsigned n=0;n<TANK_Q_CAPACITY;n++)for(unsigned k=0;k<BATTLE_ACTIONS;k++)assert(isfinite(table[n].q[k]));
        printf("FINAL seed=%u W=%u L=%u D=%u timeout=%u updates=%u\n",seeds[run],final.wins,final.losses,final.draws,final.timeouts,a.updates);printf("TABLE used=%u evictions=%u\n",a.used,a.evictions);fflush(stdout);
        assert(final.wins>baseline.wins+100 && final.losses<baseline.losses);
        assert(a.wins+a.losses+a.draws==a.episodes);
    }
    puts("PASS: three from-zero training seeds improve wins by >10 points and reduce losses; evaluation is read-only.");
    return 0;
}
