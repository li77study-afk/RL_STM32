/** 原生 C 测试：同一份算法在电脑上验证全部 100 张图，再交叉编译上板。 */
#include "q_learning.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 独立 BFS 只用于测试，不链接到固件。直接读取格子，不复用环境转移函数，
 * 避免“被测代码和测试代码犯同一个错误”而误判通过。 */
static unsigned shortest_path(unsigned *reachable)
{
    int distance[QL_STATES];
    unsigned queue[QL_STATES], head=0, tail=0;
    for (unsigned n=0;n<QL_STATES;n++) distance[n]=-1;
    queue[tail++]=QL_START; distance[QL_START]=0;
    while (head<tail) {
        unsigned s=queue[head++]; int r=(int)(s/QL_SIDE),c=(int)(s%QL_SIDE);
        const int dr[]={-1,0,1,0},dc[]={0,1,0,-1};
        for (unsigned k=0;k<4;k++) {
            int nr=r+dr[k],nc=c+dc[k];
            if(nr<0||nr>=(int)QL_SIDE||nc<0||nc>=(int)QL_SIDE||g_current_map.cells[nr][nc]=='#') continue;
            unsigned next=(unsigned)nr*QL_SIDE+(unsigned)nc;
            if(distance[next]>=0) continue;
            distance[next]=distance[s]+1; queue[tail++]=next;
        }
    }
    *reachable=tail;
    return distance[QL_GOAL]<0?0u:(unsigned)distance[QL_GOAL];
}
static void close_to(float actual,float expected) { assert(fabsf(actual-expected)<0.0001f); }

static void test_update(void)
{
    QLearner a;
    assert(Map_Init(1)); ql_init(&a,0);
    assert(a.rng!=0 && a.epsilon_permille==1000 && sizeof(a.q)==4096);
    QLTransition t=ql_transition(0,QL_UP);
    assert(t.next_state==0 && t.reward==QL_REWARD_WALL && !t.terminal);
    /* 给终点故意放一个很大的 Q 值，确认 terminal 分支不会错误地 bootstrap。 */
    a.q[QL_GOAL][0]=1000;
    t=(QLTransition){QL_GOAL,QL_REWARD_GOAL,1};
    ql_update(&a,QL_GOAL-1,QL_RIGHT,t);
    close_to(a.q[QL_GOAL-1][QL_RIGHT],6.0f); close_to(a.last_target,30.0f);
    ql_init(&a,1);
    unsigned action=0;
    while(ql_transition(0,action).next_state==0) { action++; assert(action<QL_ACTIONS); }
    t=ql_transition(0,action); a.q[t.next_state][QL_DOWN]=10; a.q[0][action]=2;
    ql_update(&a,0,action,t); close_to(a.q[0][action],3.3f);
    /* 在采样上限前一刻调用，检查回合计数和下一次调用的重启行为。 */
    ql_init(&a,1); a.steps=QL_MAX_STEPS-1;
    assert(ql_train_step(&a)); assert(a.timeouts==1 && a.episodes==1);
    ql_train_step(&a); assert(a.steps==1 && a.episodes==1);
}

static void test_environment(void)
{
    /* 穷举每张图的全部状态与动作，独立计算坐标和奖励。 */
    const int dr[]={-1,0,1,0},dc[]={0,1,0,-1};
    for (unsigned s=0;s<QL_STATES;s++) for(unsigned k=0;k<QL_ACTIONS;k++) {
        QLTransition t=ql_transition(s,k);
        unsigned next=s; int reward=QL_REWARD_WALL, terminal=0;
        int r=(int)(s/QL_SIDE),c=(int)(s%QL_SIDE);
        int nr=r+dr[k],nc=c+dc[k];
        if(s==QL_GOAL) { reward=0;terminal=1; }
        else if(g_current_map.cells[r][c]!='#' && nr>=0 && nr<(int)QL_SIDE && nc>=0 && nc<(int)QL_SIDE && g_current_map.cells[nr][nc]!='#') {
            next=(unsigned)nr*QL_SIDE+(unsigned)nc;
            terminal=next==QL_GOAL;reward=terminal?QL_REWARD_GOAL:QL_REWARD_STEP;
        }
        assert(t.next_state==next && t.reward==reward && t.terminal==(unsigned)terminal);
    }
}

int main(void)
{
    test_update();
    MapDefinition previous=g_current_map;
    assert(!Map_Init(0) && !Map_Init(MAP_COUNT+1));
    assert(memcmp(&previous,&g_current_map,sizeof(previous))==0);
    const unsigned seeds[]={QL_SEED,1u,42u};
    unsigned total_runs=0;
    for(unsigned id=1;id<=MAP_COUNT;id++) {
        assert(Map_Init(id)); assert(g_current_map.id==id);
        unsigned free_cells=0;
        for(unsigned r=0;r<QL_SIDE;r++) {
            assert(strlen(g_current_map.cells[r])==QL_SIDE);
            for(unsigned c=0;c<QL_SIDE;c++) {
                assert(g_current_map.cells[r][c]=='.'||g_current_map.cells[r][c]=='#');
                free_cells+=g_current_map.cells[r][c]=='.';
            }
        }
        unsigned reachable=0,shortest=shortest_path(&reachable);
        assert(shortest>0 && shortest==g_current_map.shortest_steps);
        assert(reachable==free_cells && free_cells==g_current_map.free_cells);
        for(unsigned old=0;old<id-1;old++)
            assert(memcmp(g_current_map.cells,g_map_library[old].cells,sizeof(g_current_map.cells))!=0);
        test_environment();
        unsigned max_updates=0;
        for(unsigned run=0;run<sizeof(seeds)/sizeof(seeds[0]);run++) {
            QLearner a,before; uint8_t path[QL_STATES+1];
            ql_init(&a,seeds[run]);
            while(a.episodes<QL_TRAIN_EPISODES) ql_train_step(&a);
            before=a;
            unsigned length=ql_evaluate(&a,path);
            assert(memcmp(&a,&before,sizeof(a))==0);
            if(length!=shortest) {
                fprintf(stderr,"FAIL map=%u seed=%u learned=%u shortest=%u successes=%u\n",id,seeds[run],length,shortest,a.successes);
                return 1;
            }
            /* 路径不能仅长度正确：逐步检查邻接和无穿墙。 */
            for(unsigned n=0;n<length;n++) {
                int r1=path[n]/QL_SIDE,c1=path[n]%QL_SIDE,r2=path[n+1]/QL_SIDE,c2=path[n+1]%QL_SIDE;
                assert((r1==r2 && (c1-c2==1||c2-c1==1)) || (c1==c2 && (r1-r2==1||r2-r1==1)));
                assert(g_current_map.cells[r2][c2]=='.');
            }
            assert(path[length]==QL_GOAL);
            assert(a.successes+a.timeouts==a.episodes && a.epsilon_permille==50);
            for(unsigned s=0;s<QL_STATES;s++) for(unsigned k=0;k<QL_ACTIONS;k++) assert(isfinite(a.q[s][k]));
            if(a.total_steps>max_updates) max_updates=a.total_steps;
            total_runs++;
        }
        printf("PASS map=%03u shortest=%u seeds=3 max_updates=%u\n",id,shortest,max_updates);
        fflush(stdout);
    }
    printf("PASS: %u unique connected maps, %u training runs all BFS-optimal; environment/TD/truncation/evaluation checks passed.\n",MAP_COUNT,total_runs);
    return 0;
}
