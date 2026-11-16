/** 用测试替身隔离检查阶段路由；生产构建不编译此文件。 */
#include "battle.h"
#include "arena_maps.h"
#include "fixed_policy.h"
#include "tank_agent.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static BattleWorld world;
static uint32_t calls[3],bad_action;

uint32_t FixedPolicy_Decide(const FixedPolicyInput *input)
{
    assert(input&&input->world&&input->state&&input->stage<=POLICY_DEMO);
    calls[input->stage]++;
    if(bad_action)return 999;
    return input->stage==POLICY_TRAIN?ACTION_UP:input->stage==POLICY_EVALUATE?ACTION_WAIT:ACTION_RIGHT_FIRE;
}

int main(void)
{
    assert(Arena_Init(&world,1));uint32_t rng=123;BattleState s;
    Battle_Reset(&world,&s,&rng);Battle_Step(&world,&s,ACTION_WAIT);
    assert(calls[POLICY_TRAIN]==1&&calls[POLICY_EVALUATE]==0&&calls[POLICY_DEMO]==0);
    Battle_Reset(&world,&s,&rng);Battle_StepWithPolicy(&world,&s,ACTION_WAIT,POLICY_DEMO);
    assert(calls[POLICY_DEMO]==1&&s.red_action==ACTION_RIGHT_FIRE);
    memset(calls,0,sizeof(calls));
    BattleScore e=TankAgent_Evaluate(&world,NULL,123,3,1);
    assert(e.games==3&&calls[POLICY_EVALUATE]>0&&calls[POLICY_TRAIN]==0&&calls[POLICY_DEMO]==0);
    memset(calls,0,sizeof(calls));
    e=TankAgent_EvaluateWithPolicy(&world,NULL,123,3,1,POLICY_TRAIN);
    assert(e.games==3&&calls[POLICY_TRAIN]>0&&calls[POLICY_EVALUATE]==0&&calls[POLICY_DEMO]==0);
    bad_action=1;Battle_Reset(&world,&s,&rng);Battle_StepWithPolicy(&world,&s,ACTION_WAIT,POLICY_DEMO);
    assert(s.red_action==ACTION_WAIT);
    puts("PASS policy routing: training/reference, evaluation/challenger, demo/challenger, invalid action fallback.");
    return 0;
}
