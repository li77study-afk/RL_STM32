/** 掩体竞技场物理与战术观测。所有仿真不依赖 HAL，运行/训练均可在 STM32。 */
#ifndef BATTLE_H
#define BATTLE_H
#include <stdint.h>
#define BATTLE_SIDE 16u
#define BATTLE_CELLS 256u
#define BATTLE_PHASES 5u
#define BATTLE_TICK_MS 100u
#define BATTLE_ACTIONS 10u
#define BATTLE_MAX_STEPS 200u
#define BATTLE_MIN_SPAWN_DISTANCE 8u /* 避免贴脸出生，留出展开与躲避时间。 */
#define BATTLE_SUBCELL 16u
#define BATTLE_BULLET_SUBSTEPS 4u
#define BATTLE_BULLET_LIMIT 16u
#define BATTLE_FIRE_COOLDOWN 5u /* 5*100 ms = 0.5 s，与旧弹是否存活无关。 */
#define BATTLE_SHOT_DIRECTIONS 16u
#define BATTLE_BULLET_COLLISIONS 3u
#define BATTLE_GAMMA 0.96f
#define BATTLE_UNREACHABLE 0xffffu
/* 前五个动作只移动/等待，后五个是同样的移动/等待加开火。 */
enum { ACTION_UP,ACTION_RIGHT,ACTION_DOWN,ACTION_LEFT,ACTION_WAIT,
       ACTION_UP_FIRE,ACTION_RIGHT_FIRE,ACTION_DOWN_FIRE,ACTION_LEFT_FIRE,ACTION_FIRE };
enum { BATTLE_RUNNING,BATTLE_BLUE_WIN,BATTLE_RED_WIN,BATTLE_MUTUAL,BATTLE_TIMEOUT };
typedef enum { BATTLE_TEAM_BLUE=0,BATTLE_TEAM_RED=1 } BattleTeam;
typedef enum { POLICY_TRAIN=0,POLICY_EVALUATE=1,POLICY_DEMO=2 } BattlePolicyStage;
typedef struct {
    uint8_t open[BATTLE_CELLS];
    uint16_t neighbor[BATTLE_CELLS][4];
    uint16_t distance[BATTLE_CELLS][BATTLE_CELLS];
    /* 低 5 位：方向+1；高位：预测撞墙次数。0=没有安全射击解。
     * 根据静态几何求炮塔射击方向，供双方共用；RL 不直接控制瞄准角。 */
    uint8_t shot_plan[BATTLE_CELLS][BATTLE_CELLS];
    uint8_t shot_time[BATTLE_CELLS][BATTLE_CELLS];
    uint16_t free_cells[BATTLE_CELLS];
    uint32_t free_count,map_id;
} BattleWorld;
typedef struct {
    uint32_t active,position,direction,collisions;
    float x,y; /* 1 格=16 单位，使用单位长度方向矢量保持车弹标称等速。 */
} BattleBullet;
typedef struct {
    uint32_t blue,red,phase,steps,outcome;
    uint32_t blue_action,red_action,blue_hit,red_hit,blocked;
    BattleBullet blue_bullets[BATTLE_BULLET_LIMIT],red_bullets[BATTLE_BULLET_LIMIT];
    uint32_t blue_cooldown,red_cooldown,blue_shots,red_shots,blue_ready,red_ready;
    uint32_t blue_bank_kills,red_bank_kills,wall_bounces;
    uint64_t observation; /* 战术状态聚合，不是所有弹丸浮点坐标的精确枚举。 */
} BattleState;
typedef struct { float reward; uint32_t terminated,truncated; } BattleTransition;
int Battle_WorldInit(BattleWorld *world,const char cells[16][17],uint32_t map_id);
uint32_t Battle_Random(uint32_t *rng);
void Battle_Reset(const BattleWorld *world,BattleState *state,uint32_t *rng);
/* 兼容接口固定调用训练基准；选择挑战策略请使用带阶段的 StepWithPolicy。 */
uint32_t Battle_RedAction(const BattleWorld *world,const BattleState *state);
uint32_t Battle_AimDirection(uint32_t origin,uint32_t target);
void Battle_DirectionVector(uint32_t direction,int *dx,int *dy);
BattleTransition Battle_StepActions(const BattleWorld *world,BattleState *state,uint32_t blue_action,uint32_t red_action);
BattleTransition Battle_Step(const BattleWorld *world,BattleState *state,uint32_t blue_action);
BattleTransition Battle_StepWithPolicy(const BattleWorld *world,BattleState *state,uint32_t blue_action,BattlePolicyStage stage);
/* 输出 5 位风险掩码：bit 0~4 对应上、右、下、左、等待。
 * 参数均须有效；函数只读输入。是否预演尚未发生的反弹由调用方显式选择。 */
void Battle_PredictThreats(const BattleWorld *world,const BattleState *state,BattleTeam team,
                          uint32_t predict_reflections,uint32_t *risk_now,uint32_t *risk_next);
uint64_t Battle_StateKey(const BattleState *state);
void Battle_Observe(const BattleWorld *world,BattleState *state);
uint32_t Battle_ActiveBullets(const BattleBullet *bullets);
#endif
