/**
 * @file tank_app.c
 * @brief 上电从零训练，自动对照随机基线，训练后循环播放双方真实策略。
 *
 * 所有学习发生在 STM32；主机的 tools/test.ps1 只是相同源码的独立验证。
 * 快速训练每批只跑有限步，不用屏幕播放速度限制算法。
 */
#include "tank_app.h"
#include "arena_maps.h"
#include "main.h"
#include <string.h>

__attribute__((section(".diagnostics"),aligned(32))) volatile TankDiagnostics g_diag;
__attribute__((section(".diagnostics"),aligned(32))) volatile uint32_t g_command;
__attribute__((section(".sdram"),aligned(32))) TankQEntry g_q_table[TANK_Q_CAPACITY];
BattleWorld g_world;BattleState g_game;TankAgent g_agent;
static BattleScore baseline,evaluation,demo;
static TankDisplayStats display;
static uint32_t mode,spawn_rng,demo_rng,intro_rng,intro_action_rng,intro_games;
static uint32_t step_tick,render_tick,publish_tick,led_tick,train_start,training_ms,update_cycles,frozen;
static uint8_t recent[100];static uint32_t recent_sum,recent_count;

void Tank_App_Init(void)
{
    /* 专用竞技场：1=分散掩体，2=反弹走位，3=错位掩体。均留有宽阔空间。 */
    if(!Arena_Init(&g_world,1))Error_Handler();
    TankAgent_Init(&g_agent,g_q_table,750);
    spawn_rng=750u^0x51a750u;demo_rng=0xd3a0750u;intro_rng=0xba55123u;intro_action_rng=0xc01750u;
    memset(&demo,0,sizeof(demo));memset(&evaluation,0,sizeof(evaluation));memset(&display,0,sizeof(display));
    memset(recent,0,sizeof(recent));recent_sum=0;recent_count=0;intro_games=0;training_ms=0;update_cycles=0;
    /* 同一个固定出生序列，先测纯随机蓝方，训练后再测无探索的贪心蓝方。
     * 评估不更新 Q 表，不消耗训练随机数。不是把演示胜利写死。 */
    baseline=TankAgent_Evaluate(&g_world,&g_agent,0xbaf123u,500,1);
    mode=MODE_BASELINE;frozen=0;g_command=0;
    Battle_Reset(&g_world,&g_game,&intro_rng);
    step_tick=HAL_GetTick();render_tick=step_tick-100u;publish_tick=step_tick-100u;led_tick=step_tick;
    g_diag.magic=0x54414e4bu;g_diag.version=4;g_diag.stage=4;
}

static const BattleBullet *representative(const BattleBullet *pool)
{
    static const BattleBullet empty={0};const BattleBullet *chosen=&empty;
    for(unsigned i=0;i<BATTLE_BULLET_LIMIT;i++)if(pool[i].active&&(!chosen->active||pool[i].collisions>chosen->collisions))chosen=pool+i;
    return chosen;
}

static void publish(uint32_t now)
{
    g_diag.sequence++;__DMB();
    g_diag.heartbeat_ms=now;g_diag.mode=mode;g_diag.map_id=g_world.map_id;g_diag.free_cells=g_world.free_count;
    g_diag.episodes=g_agent.episodes;g_diag.training_target=TANK_TRAIN_EPISODES;g_diag.updates=g_agent.updates;
    g_diag.epsilon_permille=g_agent.epsilon_permille;g_diag.train_wins=g_agent.wins;g_diag.train_losses=g_agent.losses;g_diag.train_draws=g_agent.draws;
    g_diag.recent_win_percent=recent_count?recent_sum*100u/recent_count:0;
    g_diag.baseline_games=baseline.games;g_diag.baseline_wins=baseline.wins;g_diag.baseline_losses=baseline.losses;g_diag.baseline_draws=baseline.draws;
    g_diag.eval_games=evaluation.games;g_diag.eval_wins=evaluation.wins;g_diag.eval_losses=evaluation.losses;g_diag.eval_draws=evaluation.draws;g_diag.eval_timeouts=evaluation.timeouts;
    g_diag.demo_games=demo.games;g_diag.demo_wins=demo.wins;g_diag.demo_losses=demo.losses;g_diag.demo_draws=demo.draws;
    g_diag.blue=g_game.blue;g_diag.red=g_game.red;g_diag.phase=g_game.phase;g_diag.steps=g_game.steps;g_diag.outcome=g_game.outcome;
    g_diag.blue_action=g_game.blue_action;g_diag.red_action=g_game.red_action;
    g_diag.frames=display.frames;g_diag.spi_errors=display.spi_errors;g_diag.render_us=display.render_us;
    g_diag.update_cycles=update_cycles;g_diag.training_ms=training_ms;g_diag.q_bytes=TANK_Q_BYTES;
    g_diag.core_hz=SystemCoreClock;g_diag.frozen=frozen;
    const BattleBullet *blue=representative(g_game.blue_bullets),*red=representative(g_game.red_bullets);
    g_diag.blue_bullet_active=blue->active;g_diag.blue_bullet_position=blue->position;
    g_diag.blue_bullet_direction=blue->direction;g_diag.blue_bullet_collisions=blue->collisions;
    g_diag.red_bullet_active=red->active;g_diag.red_bullet_position=red->position;
    g_diag.red_bullet_direction=red->direction;g_diag.red_bullet_collisions=red->collisions;
    g_diag.q_used=g_agent.used;g_diag.q_evictions=g_agent.evictions;
    g_diag.blue_bullet_x=(uint32_t)(blue->x*1000+0.5f);g_diag.blue_bullet_y=(uint32_t)(blue->y*1000+0.5f);
    g_diag.red_bullet_x=(uint32_t)(red->x*1000+0.5f);g_diag.red_bullet_y=(uint32_t)(red->y*1000+0.5f);
    g_diag.blue_active_bullets=Battle_ActiveBullets(g_game.blue_bullets);g_diag.red_active_bullets=Battle_ActiveBullets(g_game.red_bullets);
    g_diag.blue_shots=g_game.blue_shots;g_diag.red_shots=g_game.red_shots;
    g_diag.blue_cooldown_ms=g_game.blue_cooldown*BATTLE_TICK_MS;g_diag.red_cooldown_ms=g_game.red_cooldown*BATTLE_TICK_MS;
    g_diag.eval_blue_shots=evaluation.blue_shots;g_diag.eval_red_shots=evaluation.red_shots;
    g_diag.eval_blue_ready=evaluation.blue_ready;g_diag.eval_red_ready=evaluation.red_ready;
    g_diag.eval_blue_bank_kills=evaluation.blue_bank_kills;g_diag.eval_red_bank_kills=evaluation.red_bank_kills;
    g_diag.eval_steps=evaluation.steps;g_diag.fire_cooldown_ms=BATTLE_FIRE_COOLDOWN*BATTLE_TICK_MS;
    g_diag.tick_ms=BATTLE_TICK_MS;g_diag.speed_ratio_x1000=1000;g_diag.coordinate_scale=1000;
    __DMB();g_diag.sequence++;
}

void Tank_App_Poll(void)
{
    if(g_command==1)Tank_App_Init();
    if(g_command==2){
        frozen=1;g_command=0;
        /* 截图前先按当前逻辑状态重画，保证画面与诊断里的弹丸坐标一致。 */
        Tank_Display(mode,recent_count?recent_sum*100u/recent_count:0,&baseline,&evaluation,&demo,&display);
    }
    if(g_command==3){frozen=0;step_tick=HAL_GetTick();g_command=0;}
    uint32_t now=HAL_GetTick();
    if(!frozen) {
        if(mode==MODE_BASELINE&&now-step_tick>=(g_game.outcome?700u:BATTLE_TICK_MS)) {
            step_tick=now;
            if(g_game.outcome) {
                if(++intro_games>=2) {
                    mode=MODE_TRAIN;train_start=now;Battle_Reset(&g_world,&g_game,&spawn_rng);
                } else Battle_Reset(&g_world,&g_game,&intro_rng);
            } else Battle_StepWithPolicy(&g_world,&g_game,Battle_Random(&intro_action_rng)%BATTLE_ACTIONS,POLICY_EVALUATE);
        } else if(mode==MODE_TRAIN&&now-step_tick>=5u) {
            step_tick=now;
            for(unsigned n=0;n<256&&g_agent.episodes<TANK_TRAIN_EPISODES;n++) {
                if(g_game.outcome)Battle_Reset(&g_world,&g_game,&spawn_rng);
                uint32_t cycles=DWT->CYCCNT;
                BattleState before=g_game;unsigned action=TankAgent_Choose(&g_agent,&g_game);
                BattleTransition t=Battle_StepWithPolicy(&g_world,&g_game,action,POLICY_TRAIN);
                TankAgent_Update(&g_agent,&before,action,&g_game,t);update_cycles=DWT->CYCCNT-cycles;
                if(g_game.outcome) {
                    TankAgent_Finish(&g_agent,&g_game);
                    unsigned slot=(g_agent.episodes-1)%100;
                    recent_sum-=recent[slot];recent[slot]=g_game.outcome==BATTLE_BLUE_WIN;recent_sum+=recent[slot];
                    if(recent_count<100)recent_count++;
                }
            }
            if(g_agent.episodes>=TANK_TRAIN_EPISODES){training_ms=HAL_GetTick()-train_start;mode=MODE_EVALUATE;}
        } else if(mode==MODE_EVALUATE) {
            /* 绘制 EVALUATE 页面后再执行一次只读评估，确认已学出的真实性能。 */
            Tank_Display(mode,recent_sum,&baseline,&evaluation,&demo,&display);
            evaluation=TankAgent_Evaluate(&g_world,&g_agent,0xbaf123u,500,0);
            mode=MODE_DEMO;Battle_Reset(&g_world,&g_game,&demo_rng);step_tick=HAL_GetTick();
        } else if(mode==MODE_DEMO&&now-step_tick>=(g_game.outcome?1100u:BATTLE_TICK_MS)) {
            if(g_game.outcome){step_tick=now;Battle_Reset(&g_world,&g_game,&demo_rng);}
            else {
                step_tick+=BATTLE_TICK_MS; /* 固定时间步，避免显示开销累积拉长 0.5 s 冷却。 */
                Battle_StepWithPolicy(&g_world,&g_game,TankAgent_Greedy(&g_agent,&g_game),POLICY_DEMO);
                if(g_game.outcome)BattleScore_Add(&demo,&g_game);
            }
        }
        if(now-render_tick>=50u) {
            render_tick=now;Tank_Display(mode,recent_count?recent_sum*100u/recent_count:0,&baseline,&evaluation,&demo,&display);
        }
    }
    if(now-led_tick>=500u){HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);led_tick=now;}
    if(now-publish_tick>=100u){publish_tick=now;publish(HAL_GetTick());}
    HAL_Delay(1);
}
