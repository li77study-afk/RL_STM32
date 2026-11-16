/** 稀疏表格 Q-learning：战术聚合观测键，SDRAM 哈希表，从零初始化。 */
#ifndef TANK_AGENT_H
#define TANK_AGENT_H
#include "battle.h"
#define TANK_Q_CAPACITY (1u<<17)    /* 131072 个槽，每槽 56 字节，共 7 MiB。 */
#define TANK_Q_PROBES 16u          /* 最多探测 16 槽，限制单步最坏时间。 */
#define TANK_TRAIN_EPISODES 60000u
#define TANK_EPS_DECAY 45000u
#define TANK_ALPHA 0.18f
#define TANK_FIRE_PRIOR 0.15f /* 未学习动作的小进攻性先验，不是预训练参数。 */

typedef struct {
    uint64_t key;                   /* 0=空槽，非零为完整观测键，不把哈希碰撞当同一观测。 */
    float q[BATTLE_ACTIONS];
    uint32_t visits,reserved;
} TankQEntry;
#define TANK_Q_BYTES (TANK_Q_CAPACITY*sizeof(TankQEntry))
typedef struct {
    TankQEntry *entries;
    uint32_t rng,episodes,updates,wins,losses,draws,timeouts,epsilon_permille;
    uint32_t used,evictions;
    float last_reward,last_td;
} TankAgent;
typedef struct { uint32_t games,wins,losses,draws,timeouts,steps,blue_shots,red_shots,blue_ready,red_ready,blue_bank_kills,red_bank_kills; } BattleScore;
void TankAgent_Init(TankAgent *agent,TankQEntry *storage,uint32_t seed);
float TankAgent_Q(const TankAgent *agent,const BattleState *state,uint32_t action);
uint32_t TankAgent_Greedy(const TankAgent *agent,const BattleState *state);
uint32_t TankAgent_Choose(TankAgent *agent,const BattleState *state);
void TankAgent_Update(TankAgent *agent,const BattleState *before,uint32_t action,
                      const BattleState *after,BattleTransition transition);
void TankAgent_Finish(TankAgent *agent,const BattleState *state);
void BattleScore_Add(BattleScore *score,const BattleState *state);
/* 固定出生随机数序列，关闭探索，且绝不修改 Q 表或训练 RNG。random=1 测随机基线。 */
/* 默认评估挑战对手；需要指定阶段时使用 WithPolicy 版本。 */
BattleScore TankAgent_EvaluateWithPolicy(const BattleWorld *world,const TankAgent *agent,uint32_t seed,
                                        uint32_t games,int random,BattlePolicyStage stage);
BattleScore TankAgent_Evaluate(const BattleWorld *world,const TankAgent *agent,uint32_t seed,uint32_t games,int random);
#endif
