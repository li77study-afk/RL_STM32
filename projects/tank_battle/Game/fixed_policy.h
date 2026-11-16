/**
 * @file fixed_policy.h
 * @brief 固定红方策略的唯一开发接口，修改策略请优先编辑 fixed_policy.c。
 *
 * 输入：只读的地图/几何数据、当前拍开始前的完整对战状态、调用阶段。
 * 输出：一个 0~9 的动作编号，移动与开火的编码见下方说明。
 *
 * 每个仿真拍调用一次；训练可能加速，不能用 HAL_GetTick 或真实睡眠计时。
 * 不修改输入，不移动坦克，不创建弹丸，不直接改冷却/胜负，也不读取 Q 表。
 */
#ifndef FIXED_POLICY_H
#define FIXED_POLICY_H
#include "battle.h"

typedef struct {
    const BattleWorld *world;       /* 已初始化的地图、邻居、距离和射击解。 */
    const BattleState *state;       /* 同步移动/冷却递减/发射之前的状态快照。 */
    BattlePolicyStage stage;        /* TRAIN=训练基准；EVALUATE/DEMO=可修改挑战策略。 */
} FixedPolicyInput;

/**
 * 返回值：
 * 0/1/2/3/4 = 上/右/下/左/等待，不申请开火。
 * 5/6/7/8/9 = 上/右/下/左/等待，同时申请开火。
 * 即 movement=action%5，request_fire=(action>=5)。建议返回 ACTION_* 枚举。
 *
 * 输入中 self=state->red，enemy=state->blue。
 * red_cooldown<=1 表示本拍结算时能够开火，因为物理层会先递减一次冷却。
 * 返回开火请求不保证实际发射：冷却、弹仓和死亡检查仍由物理层执行。
 * 炮塔方向由共用射击几何决定，本接口不输出瞄准角度。
 *
 * 无效输入、已结束回合、未知阶段或自定义策略返回非法动作时，返回 WAIT。
 */
uint32_t FixedPolicy_Decide(const FixedPolicyInput *input);
#endif
