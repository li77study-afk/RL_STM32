/**
 * @file battle.h
 * @brief 16×16 双坦克迷宫的确定性、同步结算环境。无 HAL 依赖。
 *
 * 状态包含双方位置、红方射击时钟及双方飞行子弹的完整状态。
 * 蓝方是学习者，红方是固定规则对手。双方能观察完整地图和位置。
 */
#ifndef BATTLE_H
#define BATTLE_H
#include <stdint.h>
#define BATTLE_SIDE 16u
#define BATTLE_CELLS 256u
#define BATTLE_PHASES 4u
#define BATTLE_ACTIONS 6u
#define BATTLE_MAX_STEPS 160u
#define BATTLE_BULLET_SUBSTEPS 4u /* 每拍 4 个半格子步，弹速约为坦克的两倍。 */
#define BATTLE_SUBCELL 16u          /* 每个地图格细分 16 单位，位置为 0~255。 */
#define BATTLE_SHOT_DIRECTIONS 16u
#define BATTLE_BULLET_COLLISIONS 3u /* 第 3 次撞墙时消失，前两次反弹。 */
#define BATTLE_GAMMA 0.96f
#define BATTLE_UNREACHABLE 0xffffu
enum { ACTION_UP, ACTION_RIGHT, ACTION_DOWN, ACTION_LEFT, ACTION_WAIT, ACTION_FIRE };
enum { BATTLE_RUNNING, BATTLE_BLUE_WIN, BATTLE_RED_WIN, BATTLE_MUTUAL, BATTLE_TIMEOUT };
typedef struct {
    uint8_t open[BATTLE_CELLS];
    uint16_t neighbor[BATTLE_CELLS][4]; /* 撞墙方向的邻居等于自身。 */
    uint16_t distance[BATTLE_CELLS][BATTLE_CELLS]; /* BFS 距离供固定对手与奖励使用。 */
    uint8_t visible[BATTLE_CELLS][BATTLE_CELLS];  /* 十六方向直视且中间无墙。 */
    uint16_t free_cells[BATTLE_CELLS];
    uint32_t free_count,map_id;
} BattleWorld;
typedef struct {
    uint32_t active,position,direction,collisions,x,y;
    /* 0 上、4 右、8 下、12 左，其余为斜向，共 16 档。
     * x/y 是细分坐标；position 是由 x/y 派生的格子编号，仅用于显示/诊断。 */
} BattleBullet;
typedef struct {
    uint32_t blue,red,phase,steps,outcome;
    uint32_t blue_action,red_action,blue_hit,red_hit,blocked;
    BattleBullet blue_bullet,red_bullet;
} BattleState;
typedef struct { float reward; uint32_t terminated,truncated; } BattleTransition;
/* 世界只读表只算一次，不包含蓝方的最优动作或训练答案。 */
int Battle_WorldInit(BattleWorld *world,const char cells[16][17],uint32_t map_id);
uint32_t Battle_Random(uint32_t *rng);
void Battle_Reset(const BattleWorld *world,BattleState *state,uint32_t *rng);
uint32_t Battle_RedAction(const BattleWorld *world,const BattleState *state);
uint32_t Battle_AimDirection(uint32_t origin,uint32_t target);
void Battle_DirectionVector(uint32_t direction,int *dx,int *dy);
/* 显式输入双方动作，便于独立测试同步移动、飞行、反弹和逐格命中。 */
BattleTransition Battle_StepActions(const BattleWorld *world,BattleState *state,uint32_t blue_action,uint32_t red_action);
BattleTransition Battle_Step(const BattleWorld *world,BattleState *state,uint32_t blue_action);
/* 完整状态编码为不超过 64 位的键，包含两颗子弹；0 保留给空哈希槽。
 * 子弹不活动时忽略其残留位置/方向，避免为相同物理状态产生多个键。 */
uint64_t Battle_StateKey(const BattleState *state);
#endif
