#ifndef Q_APP_H
#define Q_APP_H
#include "q_learning.h"
/* All fields are 32-bit, published with an even/odd sequence for SWD reads. */
typedef struct {
    uint32_t sequence, magic, version, stage, fatal_count, heartbeat_ms;
    uint32_t mode, episodes, successes, timeouts, total_steps, state, steps;
    uint32_t epsilon_permille, last_steps;
    int32_t last_return;
    uint32_t eval_steps, frames, spi_errors, update_cycles, render_us;
    uint32_t training_ms, recent_success_percent, demo_runs, core_hz;
} QDiagnostics;
extern volatile QDiagnostics g_diag;
extern QLearner g_agent;
extern volatile uint32_t g_command;
enum { MODE_TRAIN = 0, MODE_DEMO = 1, MODE_UNSOLVED = 2 };
void Q_App_Init(void);
void Q_App_Poll(void);
typedef struct { uint32_t frames, spi_errors, render_us; } QDisplayStats;
void Q_Display_Render(uint32_t mode, uint32_t state, const uint8_t *path,
                      uint32_t path_steps, uint32_t recent_success, QDisplayStats *stats);
#endif
