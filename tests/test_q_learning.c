#include "q_learning.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Independent breadth-first search is a test oracle only, never linked into
 * the firmware. The learner receives no shortest-path information. */
static unsigned shortest_path(void)
{
    int distance[64]; unsigned queue[64], head=0, tail=0;
    for (unsigned n=0;n<64;n++) distance[n]=-1;
    queue[tail++]=QL_START; distance[QL_START]=0;
    while (head<tail) {
        unsigned s=queue[head++]; int r=(int)s/8,c=(int)s%8;
        if (s==QL_GOAL) return (unsigned)distance[s];
        const int dr[]={-1,0,1,0},dc[]={0,1,0,-1};
        for (unsigned k=0;k<4;k++) {
            int nr=r+dr[k],nc=c+dc[k];
            if(nr<0||nr>=8||nc<0||nc>=8||ql_maze[nr][nc]=='#') continue;
            unsigned next=(unsigned)(nr*8+nc);
            if(distance[next]>=0) continue;
            distance[next]=distance[s]+1; queue[tail++]=next;
        }
    }
    return 0;
}
static void close_to(float actual,float expected) { assert(fabsf(actual-expected)<0.0001f); }

int main(void)
{
    QLearner a, before;
    uint8_t path[65];
    ql_init(&a,0);
    assert(a.rng!=0 && a.epsilon_permille==1000 && sizeof(a.q)==1024);
    QLTransition t=ql_transition(0,QL_UP);
    assert(t.next_state==0 && t.reward==QL_REWARD_WALL && !t.terminal);
    t=ql_transition(0,QL_DOWN);
    assert(t.next_state==0 && t.reward==QL_REWARD_WALL);
    t=ql_transition(0,QL_RIGHT);
    assert(t.next_state==1 && t.reward==QL_REWARD_STEP);
    t=ql_transition(62,QL_RIGHT);
    assert(t.next_state==QL_GOAL && t.terminal && t.reward==QL_REWARD_GOAL);
    a.q[QL_GOAL][0]=1000; /* Must not leak into the terminal TD target. */
    ql_update(&a,62,QL_RIGHT,t);
    close_to(a.q[62][QL_RIGHT],6.0f); close_to(a.last_target,30.0f);
    ql_init(&a,1); a.q[1][QL_DOWN]=10; a.q[0][QL_RIGHT]=2;
    ql_update(&a,0,QL_RIGHT,ql_transition(0,QL_RIGHT));
    close_to(a.q[0][QL_RIGHT],3.3f);
    ql_init(&a,1); a.steps=QL_MAX_STEPS-1;
    assert(ql_train_step(&a)); assert(a.timeouts==1 && a.episodes==1);
    ql_train_step(&a); assert(a.steps==1 && a.episodes==1);
    unsigned shortest=shortest_path(); assert(shortest>0);
    unsigned runs=33, min_success=1500, max_steps=0;
    for(unsigned run=0;run<runs;run++) {
        unsigned seed=run==0?QL_SEED:run;
        ql_init(&a,seed);
        while(a.episodes<QL_TRAIN_EPISODES) ql_train_step(&a);
        before=a;
        unsigned length=ql_evaluate(&a,path);
        assert(memcmp(&a,&before,sizeof(a))==0);
        if(length!=shortest) {
            fprintf(stderr,"seed=%u learned=%u shortest=%u\n",seed,length,shortest);
            return 1;
        }
        assert(a.successes+a.timeouts==a.episodes && a.epsilon_permille==50);
        for(unsigned s=0;s<64;s++) for(unsigned k=0;k<4;k++) assert(isfinite(a.q[s][k]));
        if(a.successes<min_success) min_success=a.successes;
        if(a.total_steps>max_steps) max_steps=a.total_steps;
        if(run==0) printf("default_seed=%u episodes=%u successes=%u timeouts=%u updates=%u path_steps=%u\n",
                            seed,a.episodes,a.successes,a.timeouts,a.total_steps,length);
    }
    printf("PASS: transitions, terminal target, TD update, truncation, read-only evaluation.\n");
    printf("PASS: %u/%u seeds learned BFS-optimal %u-step path; min_successes=%u max_updates=%u.\n",
           runs,runs,shortest,min_success,max_steps);
    return 0;
}
