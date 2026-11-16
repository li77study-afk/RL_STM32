/** 板级应用：训练/评估/演示状态机。运行不需要串口或电脑。 */
#ifndef TANK_APP_H
#define TANK_APP_H
#include "tank_agent.h"
enum { MODE_BASELINE,MODE_TRAIN,MODE_EVALUATE,MODE_DEMO };
typedef struct {
    uint32_t sequence,magic,version,stage,fatal_count,heartbeat_ms;
    uint32_t sdram_ok,sdram_error_addr,mode,map_id,free_cells;
    uint32_t episodes,training_target,updates,epsilon_permille,train_wins,train_losses,train_draws;
    uint32_t recent_win_percent,baseline_games,baseline_wins,baseline_losses,baseline_draws;
    uint32_t eval_games,eval_wins,eval_losses,eval_draws,eval_timeouts;
    uint32_t demo_games,demo_wins,demo_losses,demo_draws;
    uint32_t blue,red,phase,steps,outcome,blue_action,red_action;
    uint32_t frames,spi_errors,render_us,update_cycles,training_ms,q_bytes,core_hz,frozen;
    uint32_t blue_bullet_active,blue_bullet_position,blue_bullet_direction,blue_bullet_collisions;
    uint32_t red_bullet_active,red_bullet_position,red_bullet_direction,red_bullet_collisions;
    uint32_t q_used,q_evictions;
    uint32_t blue_bullet_x,blue_bullet_y,red_bullet_x,red_bullet_y;
} TankDiagnostics;
typedef struct { uint32_t frames,spi_errors,render_us; } TankDisplayStats;
extern volatile TankDiagnostics g_diag;
extern volatile uint32_t g_command;
extern BattleWorld g_world;
extern BattleState g_game;
extern TankAgent g_agent;
void Board_SDRAM_Init(void);
void Tank_App_Init(void);
void Tank_App_Poll(void);
void Tank_Display(uint32_t mode,uint32_t recent,const BattleScore *baseline,
                  const BattleScore *evaluation,const BattleScore *demo,TankDisplayStats *stats);
#endif
