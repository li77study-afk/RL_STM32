#include "tank_agent.h"
#include <string.h>

_Static_assert(sizeof(TankQEntry)==40,"Q table memory budget assumes 40-byte entries");

static uint32_t hash_key(uint64_t key)
{
    uint32_t h=(uint32_t)key^((uint32_t)(key>>32)*0x9e3779b9u);
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return h;
}

static const TankQEntry *find(const TankAgent *a,uint64_t key)
{
    unsigned start=hash_key(key)&(TANK_Q_CAPACITY-1);
    for(unsigned n=0;n<TANK_Q_PROBES;n++) {
        const TankQEntry *entry=a->entries+((start+n)&(TANK_Q_CAPACITY-1));
        if(entry->key==key)return entry;
        if(!entry->key)return NULL;
    }
    return NULL;
}

static TankQEntry *create(TankAgent *a,uint64_t key)
{
    unsigned start=hash_key(key)&(TANK_Q_CAPACITY-1);TankQEntry *victim=NULL;
    for(unsigned n=0;n<TANK_Q_PROBES;n++) {
        TankQEntry *entry=a->entries+((start+n)&(TANK_Q_CAPACITY-1));
        if(entry->key==key)return entry;
        if(!entry->key){a->used++;victim=entry;break;}
        if(!victim||entry->visits<victim->visits)victim=entry;
    }
    if(victim->key)a->evictions++;
    memset(victim,0,sizeof(*victim));victim->key=key;return victim;
}

void TankAgent_Init(TankAgent *a,TankQEntry *storage,uint32_t seed)
{
    memset(a,0,sizeof(*a));a->entries=storage;a->rng=seed?seed:1;
    memset(storage,0,TANK_Q_BYTES);a->epsilon_permille=1000;
}

float TankAgent_Q(const TankAgent *a,const BattleState *s,uint32_t action)
{
    const TankQEntry *entry=find(a,Battle_StateKey(s));
    return entry&&action<BATTLE_ACTIONS?entry->q[action]:0.0f;
}

uint32_t TankAgent_Greedy(const TankAgent *a,const BattleState *s)
{
    uint64_t key=Battle_StateKey(s);const TankQEntry *entry=find(a,key);
    /* 未学过的状态使用由状态键决定的固定平局顺序，不隐藏寻路/躲避规则。
     * 这样初始全零不会总朝上走，且评估不修改训练 RNG。 */
    unsigned start=hash_key(key)%BATTLE_ACTIONS,best=start;
    if(entry)for(unsigned n=1;n<BATTLE_ACTIONS;n++) {
        unsigned k=(start+n)%BATTLE_ACTIONS;
        if(entry->q[k]>entry->q[best])best=k;
    }
    return best;
}

uint32_t TankAgent_Choose(TankAgent *a,const BattleState *s)
{
    a->epsilon_permille=a->episodes>=TANK_EPS_DECAY?30u:1000u-a->episodes*970u/TANK_EPS_DECAY;
    if(Battle_Random(&a->rng)%1000u<a->epsilon_permille)return Battle_Random(&a->rng)%BATTLE_ACTIONS;
    TankQEntry *entry=create(a,Battle_StateKey(s));
    float best=entry->q[TankAgent_Greedy(a,s)];unsigned choices[BATTLE_ACTIONS],count=0;
    for(unsigned k=0;k<BATTLE_ACTIONS;k++)if(entry->q[k]==best)choices[count++]=k;
    return choices[Battle_Random(&a->rng)%count];
}

void TankAgent_Update(TankAgent *a,const BattleState *before,uint32_t action,const BattleState *after,BattleTransition t)
{
    float future=0;
    if(!t.terminated) {
        const TankQEntry *next=find(a,Battle_StateKey(after));
        if(next){future=next->q[0];for(unsigned k=1;k<BATTLE_ACTIONS;k++)if(next->q[k]>future)future=next->q[k];}
    }
    TankQEntry *entry=create(a,Battle_StateKey(before));float *value=entry->q+action;
    float td=t.reward+BATTLE_GAMMA*future-*value;
    *value+=TANK_ALPHA*td;entry->visits++;a->last_reward=t.reward;a->last_td=td;a->updates++;
}

void TankAgent_Finish(TankAgent *a,const BattleState *s)
{
    a->episodes++;
    if(s->outcome==BATTLE_BLUE_WIN)a->wins++;
    else if(s->outcome==BATTLE_RED_WIN)a->losses++;
    else {a->draws++;a->timeouts+=s->outcome==BATTLE_TIMEOUT;}
}

void BattleScore_Add(BattleScore *score,const BattleState *s)
{
    score->games++;score->steps+=s->steps;
    if(s->outcome==BATTLE_BLUE_WIN)score->wins++;
    else if(s->outcome==BATTLE_RED_WIN)score->losses++;
    else {score->draws++;score->timeouts+=s->outcome==BATTLE_TIMEOUT;}
}

BattleScore TankAgent_Evaluate(const BattleWorld *w,const TankAgent *a,uint32_t seed,uint32_t games,int random)
{
    BattleScore score={0};uint32_t spawn_rng=seed?seed:1,action_rng=seed^0xa17e531u;
    for(unsigned n=0;n<games;n++) {
        BattleState s;Battle_Reset(w,&s,&spawn_rng);
        while(s.outcome==BATTLE_RUNNING) {
            unsigned action=random?Battle_Random(&action_rng)%BATTLE_ACTIONS:TankAgent_Greedy(a,&s);
            Battle_Step(w,&s,action);
        }
        BattleScore_Add(&score,&s);
    }
    return score;
}
