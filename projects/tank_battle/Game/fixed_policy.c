/**
 * @file fixed_policy.c
 * @brief 红方固定程序决策。训练基准和挑战策略默认一致，便于纯重构回归。
 *
 * 鲁棒性实验：只改 challenge_config / challenge_policy()。
 * 保留 reference_config / reference_policy()，蓝方仍对同一个基准训练，
 * 然后在 EVALUATE 和 DEMO 阶段面对你修改过的新对手。
 *
 * 完整输入输出说明与可直接替换的示例：docs/FIXED_POLICY_API.md。
 */
#include "fixed_policy.h"

typedef struct {
    uint32_t path_weight;           /* 接近对手的距离代价，越大越倾向追击。 */
    uint32_t immediate_risk_weight; /* 本拍来弹代价，越大越重视立即躲避。 */
    uint32_t future_risk_weight;    /* 下一拍来弹代价，越大越提前避弹。 */
    uint32_t blocked_move_penalty;  /* 朝墙移动的代价。 */
    uint32_t blind_fire_range;      /* 无几何射击解时，允许盲射的最大路径距离。 */
    uint32_t predict_reflections;   /* 1=预演未来反弹；0=仅沿当前方向反应。 */
    uint32_t fire_enabled;          /* 0=不申请开火；1=冷却就绪且满足条件时开火。 */
} FixedPolicyConfig;

/* 训练基准区：做跨对手鲁棒性实验时保留这里。 */
static const FixedPolicyConfig reference_config={
    .path_weight=4,
    .immediate_risk_weight=100,
    .future_risk_weight=20,
    .blocked_move_penalty=50,
    .blind_fire_range=8,
    .predict_reflections=0,
    .fire_enabled=1
};

/* ==================== 你主要修改这里 ====================
 * 这些参数只影响挑战对手，不改变训练基准。
 * 例如提高盲射距离可增加进攻性；提高风险权重可强化躲避；
 * predict_reflections=1 会让挑战对手提前考虑墙面反弹。
 * 需要全新决策逻辑时，修改下方 challenge_policy() 函数体。
 */
static const FixedPolicyConfig challenge_config={
    .path_weight=4,
    .immediate_risk_weight=100,
    .future_risk_weight=20,
    .blocked_move_penalty=50,
    .blind_fire_range=8,
    .predict_reflections=0,
    .fire_enabled=1
};

static uint32_t choose_with_config(const FixedPolicyInput *input,const FixedPolicyConfig *config)
{
    const BattleWorld *world=input->world;
    const BattleState *state=input->state;
    uint32_t risk_now,risk_next;
    /* 物理层只提供通用预测工具；是否预测未来反弹由策略文件决定。 */
    Battle_PredictThreats(world,state,BATTLE_TEAM_RED,config->predict_reflections,&risk_now,&risk_next);

    uint32_t best=ACTION_WAIT,best_cost=UINT32_MAX;
    for(uint32_t move=ACTION_UP;move<=ACTION_WAIT;move++) {
        uint32_t next=move<ACTION_WAIT?world->neighbor[state->red][move]:state->red;
        if(next==state->blue)continue;
        uint32_t cost=world->distance[next][state->blue]*config->path_weight
            +((risk_now>>move)&1u)*config->immediate_risk_weight
            +((risk_next>>move)&1u)*config->future_risk_weight;
        if(move<ACTION_WAIT&&next==state->red)cost+=config->blocked_move_penalty;
        /* 相等时保留上、右、下、左、等待的固定顺序，保证可复现实验。 */
        if(cost<best_cost){best_cost=cost;best=move;}
    }

    /* 开火角度由物理层根据移动后的双方位置选取；策略只申请是否发射。
     * 这里的 source 只是假设自己移动到的格子，对手仍可能同步移动。 */
    uint32_t source=best<ACTION_WAIT?world->neighbor[state->red][best]:state->red;
    uint32_t can_request_fire=state->red_cooldown<=1;
    uint32_t has_shot=world->shot_plan[source][state->blue]!=0;
    uint32_t close_enough=world->distance[source][state->blue]<=config->blind_fire_range;
    uint32_t fire=config->fire_enabled&&can_request_fire&&(has_shot||close_enough);
    return best+(fire?5u:0u);
}

static uint32_t reference_policy(const FixedPolicyInput *input)
{
    return choose_with_config(input,&reference_config);
}

static uint32_t challenge_policy(const FixedPolicyInput *input)
{
    /* 可把此函数体整个替换为你自己的固定策略。
     * 最小例子：return ACTION_FIRE; 表示原地持续申请开火。
     * 不必自己等待 500 ms，物理层会处理冷却与弹丸池。
     */
    return choose_with_config(input,&challenge_config);
}

uint32_t FixedPolicy_Decide(const FixedPolicyInput *input)
{
    /* 允许把挑战函数完全换成自定义逻辑，同时保留配置区而不产生未使用警告。 */
    (void)challenge_config;
    if(!input||!input->world||!input->state)return ACTION_WAIT;
    const BattleState *state=input->state;
    if(state->outcome!=BATTLE_RUNNING||state->red>=BATTLE_CELLS||state->blue>=BATTLE_CELLS)return ACTION_WAIT;
    if(state->red==state->blue||!input->world->open[state->red]||!input->world->open[state->blue])return ACTION_WAIT;

    uint32_t action;
    if(input->stage==POLICY_TRAIN)action=reference_policy(input);
    else if(input->stage==POLICY_EVALUATE||input->stage==POLICY_DEMO)action=challenge_policy(input);
    else return ACTION_WAIT;
    return action<BATTLE_ACTIONS?action:ACTION_WAIT;
}
