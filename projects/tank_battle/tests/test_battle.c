/** 掩体/同速/连发/多弹丸规则及原生学习对照。 */
#include "battle.h"
#include "tank_agent.h"
#include "arena_maps.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static BattleWorld world;
static TankQEntry table[TANK_Q_CAPACITY];
static BattleState setup(unsigned blue,unsigned red)
{
    BattleState s={0};s.blue=blue;s.red=red;s.blue_action=ACTION_WAIT;s.red_action=ACTION_WAIT;Battle_Observe(&world,&s);return s;
}
static BattleBullet shot(unsigned cell,unsigned d,unsigned collisions)
{
    BattleBullet b={0};b.active=1;b.position=cell;b.direction=d;b.collisions=collisions;b.x=(cell%16)*16+8;b.y=(cell/16)*16+8;return b;
}
static int has_square(unsigned pos)
{
    int r=(int)(pos/16),c=(int)(pos%16);
    for(int dr=-1;dr<=0;dr++)for(int dc=-1;dc<=0;dc++) {
        int y=r+dr,x=c+dc;if(y<0||x<0||y>=15||x>=15)continue;
        if(world.open[y*16+x]&&world.open[y*16+x+1]&&world.open[(y+1)*16+x]&&world.open[(y+1)*16+x+1])return 1;
    }
    return 0;
}
static unsigned clearance_squares(void)
{
    unsigned char open[225]={0},seen[225]={0};unsigned queue[225],head=0,tail=0,total=0;
    for(unsigned r=0;r<15;r++)for(unsigned c=0;c<15;c++) {
        unsigned s=r*16+c,index=r*15+c;
        if(world.open[s]&&world.open[s+1]&&world.open[s+16]&&world.open[s+17]){open[index]=1;total++;}
    }
    for(unsigned n=0;n<225;n++)if(open[n]){seen[n]=1;queue[tail++]=n;break;}
    while(head<tail){unsigned p=queue[head++];int r=(int)(p/15),c=(int)(p%15);
        const int dr[4]={-1,0,1,0},dc[4]={0,1,0,-1};
        for(unsigned a=0;a<4;a++){int y=r+dr[a],x=c+dc[a];if(y<0||y>=15||x<0||x>=15)continue;
            unsigned next=(unsigned)y*15+(unsigned)x;if(open[next]&&!seen[next]){seen[next]=1;queue[tail++]=next;}
        }
    }
    assert(tail==total&&total>0);return total;
}

static void rules(void)
{
    for(unsigned id=1;id<=ARENA_COUNT;id++) {
        assert(Arena_Init(&world,id));unsigned bank=0;
        for(unsigned s=0;s<256;s++) {
            if(world.open[s])assert(has_square(s)); /* 每个道路格属于至少一块 2×2 空间。 */
            for(unsigned t=0;t<256;t++)bank+=world.shot_plan[s][t]>>5!=0;
        }
        assert(bank>0);printf("ARENA %u free=%u bank_solutions=%u connected_2x2_squares=%u\n",id,world.free_count,bank,clearance_squares());
    }
    char map[16][17];for(unsigned r=0;r<16;r++){memset(map[r],'.',16);map[r][16]=0;}
    assert(Battle_WorldInit(&world,map,0));
    /* 所有 16 档子弹在无碰撞时每拍走 16 单位，与坦克一格一致。 */
    for(unsigned d=0;d<16;d++) {
        BattleState s=setup(0,255);s.blue_bullets[0]=shot(119,d,0);
        float x=s.blue_bullets[0].x,y=s.blue_bullets[0].y;
        Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
        float dx=s.blue_bullets[0].x-x,dy=s.blue_bullets[0].y-y;
        assert(s.blue_bullets[0].active&&fabsf(sqrtf(dx*dx+dy*dy)-16.0f)<0.001f);
    }
    BattleState s=setup(17,255);Battle_StepActions(&world,&s,ACTION_RIGHT,ACTION_WAIT);assert(s.blue==18);
    /* 连续请求开火：第 0、5、10 拍发射，旧弹仍在飞；不是等旧弹消失。 */
    s=setup(17,255);
    for(unsigned n=0;n<11;n++) {
        Battle_StepActions(&world,&s,ACTION_FIRE,ACTION_WAIT);
        assert(s.blue_shots==1+n/5);
        assert(!s.outcome);
    }
    assert(Battle_ActiveBullets(s.blue_bullets)>=3);
    /* 边走边射，移动与开火不互斥。 */
    s=setup(17,255);Battle_StepActions(&world,&s,ACTION_RIGHT_FIRE,ACTION_WAIT);
    assert(s.blue==18&&s.blue_shots==1);
    s=setup(17,19);Battle_StepActions(&world,&s,ACTION_RIGHT,ACTION_LEFT);assert(s.blue==17&&s.red==19);
    s=setup(17,18);Battle_StepActions(&world,&s,ACTION_RIGHT,ACTION_LEFT);assert(s.blue==17&&s.red==18);
    /* 第三次墙碰消失，途中前两次保持活动。 */
    map[1][1]='#';map[1][3]='#';assert(Battle_WorldInit(&world,map,0));
    s=setup(64,255);s.blue_bullets[0]=shot(18,4,0);unsigned observed=0;
    for(unsigned n=0;n<12&&s.blue_bullets[0].active;n++) {
        Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
        if(s.blue_bullets[0].active)observed|=1u<<s.blue_bullets[0].collisions;
    }
    assert(!s.blue_bullets[0].active&&s.blue_bullets[0].collisions==3&&(observed&6u)==6u);
    /* 反弹自弹命中、逐段命中和超时 bootstrap 规则。 */
    s=setup(33,255);s.blue_bullets[0]=shot(34,12,1);
    for(unsigned n=0;n<3&&!s.outcome;n++)Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
    assert(s.outcome==BATTLE_RED_WIN);
    s=setup(64,82);s.blue_bullets[0]=shot(81,4,0);Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);assert(s.outcome==BATTLE_BLUE_WIN);
    s=setup(64,255);s.steps=BATTLE_MAX_STEPS-1;
    BattleTransition t=Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);assert(t.truncated&&!t.terminated);
    /* 使用预计算反弹解，静止双方应真实产生一次反弹击杀，而不是只有解码标记。 */
    assert(Arena_Init(&world,1));int verified=0;
    for(unsigned a=0;a<256&&!verified;a++)for(unsigned b=0;b<256&&!verified;b++)if(world.shot_plan[a][b]>>5) {
        s=setup(a,b);Battle_StepActions(&world,&s,ACTION_FIRE,ACTION_WAIT);
        while(!s.outcome)Battle_StepActions(&world,&s,ACTION_WAIT,ACTION_WAIT);
        if(s.outcome==BATTLE_BLUE_WIN&&s.blue_bank_kills)verified=1;
    }
    assert(verified);
    TankAgent agent;TankAgent_Init(&agent,table,1);BattleState before=setup(17,255),after=setup(18,255);
    before.observation=123;after.observation=456;
    t=(BattleTransition){10,1,0};TankAgent_Update(&agent,&before,ACTION_UP,&after,t);
    assert(fabsf(TankAgent_Q(&agent,&before,ACTION_UP)-1.8f)<0.0001f);
    TankAgent_Init(&agent,table,1);t=(BattleTransition){10.0f/TANK_ALPHA,1,0};TankAgent_Update(&agent,&after,ACTION_UP,&after,t);
    t=(BattleTransition){-1,0,1};TankAgent_Update(&agent,&before,ACTION_UP,&after,t);
    assert(fabsf(TankAgent_Q(&agent,&before,ACTION_UP)-1.548f)<0.0001f);
    puts("PASS rules: cover space, equal speeds, 500ms overlapping fire, move+fire, ricochet kill, 3 collisions, TD.");
}
static uint32_t hash_table(void)
{
    uint32_t h=2166136261u;const unsigned char *p=(const unsigned char *)table;
    for(unsigned n=0;n<sizeof(table);n++)h=(h^p[n])*16777619u;return h;
}
int main(void)
{
    rules();assert(Arena_Init(&world,1));assert(sizeof(table)==7u*1024u*1024u);
    unsigned seeds[3]={750,1,42};
    for(unsigned run=0;run<3;run++) {
        TankAgent a;TankAgent_Init(&a,table,seeds[run]);unsigned rng=seeds[run]^0x51a750u;
        BattleScore base=TankAgent_Evaluate(&world,&a,0xbaf123u,500,1);
        BattleScore prior=TankAgent_Evaluate(&world,&a,0xbaf123u,500,0);
        printf("UNTRAINED seed=%u W=%u L=%u D=%u shots=%u\n",seeds[run],prior.wins,prior.losses,prior.draws,prior.blue_shots);
        printf("BASE seed=%u W=%u L=%u D=%u shots=%u ready=%u bank=%u\n",seeds[run],base.wins,base.losses,base.draws,base.blue_shots,base.blue_ready,base.blue_bank_kills);fflush(stdout);
        while(a.episodes<TANK_TRAIN_EPISODES) {
            BattleState game;Battle_Reset(&world,&game,&rng);
            while(!game.outcome) {
                BattleState before=game;unsigned action=TankAgent_Choose(&a,&game);BattleTransition t=Battle_Step(&world,&game,action);
                TankAgent_Update(&a,&before,action,&game,t);
                assert(world.open[game.blue]&&world.open[game.red]&&game.blue!=game.red);
                for(unsigned team=0;team<2;team++)for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++) {
                    const BattleBullet *b=team?game.red_bullets+i:game.blue_bullets+i;
                    if(b->active)assert(b->x>=0&&b->x<256&&b->y>=0&&b->y<256&&world.open[b->position]&&b->collisions<3&&b->direction<16);
                }
            }
            TankAgent_Finish(&a,&game);
            if(a.episodes%10000==0){BattleScore e=TankAgent_Evaluate(&world,&a,0x7155u,200,0);printf("TRAIN seed=%u ep=%u W=%u/200 shots=%u ready=%u bank=%u\n",seeds[run],a.episodes,e.wins,e.blue_shots,e.blue_ready,e.blue_bank_kills);fflush(stdout);}
        }
        uint32_t hash=hash_table();TankAgent saved=a;BattleScore e=TankAgent_Evaluate(&world,&a,0xbaf123u,500,0);
        assert(hash==hash_table()&&memcmp(&a,&saved,sizeof(a))==0);
        for(unsigned n=0;n<TANK_Q_CAPACITY;n++)for(unsigned k=0;k<BATTLE_ACTIONS;k++)assert(isfinite(table[n].q[k]));
        printf("FINAL seed=%u W=%u L=%u D=%u shots=%u ready=%u bank=%u steps=%u used=%u evict=%u updates=%u\n",seeds[run],e.wins,e.losses,e.draws,e.blue_shots,e.blue_ready,e.blue_bank_kills,e.steps,a.used,a.evictions,a.updates);fflush(stdout);
        assert(e.wins>base.wins+50&&e.wins>prior.wins+50);
    }
    puts("PASS learning: three seeds improve win rate; evaluation does not train.");return 0;
}
