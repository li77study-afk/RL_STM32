#include "q_app.h"
#include "main.h"
#include <string.h>

__attribute__((section(".diagnostics"), aligned(32))) volatile QDiagnostics g_diag;
__attribute__((section(".diagnostics"), aligned(32))) QLearner g_agent;
__attribute__((section(".diagnostics"), aligned(32))) volatile uint32_t g_command;
static uint8_t path[QL_STATES + 1], recent[100];
static uint32_t mode, path_steps, demo_index, recent_sum, recent_count;
static uint32_t start_tick, step_tick, render_tick, led_tick;
static uint32_t report_tick, update_cycles, training_ms, demo_runs;
static QDisplayStats display_stats;

void Q_App_Init(void)
{
    ql_init(&g_agent, QL_SEED);
    memset(recent, 0, sizeof(recent));
    mode = MODE_TRAIN; path_steps = 0; demo_index = 0;
    recent_sum = 0; recent_count = 0;
    start_tick = HAL_GetTick(); step_tick = start_tick;
    render_tick = start_tick - 100; led_tick = start_tick;
    report_tick = start_tick - 100;
    update_cycles = 0; training_ms = 0; demo_runs = 0;
    memset(&display_stats, 0, sizeof(display_stats));
    g_command = 0;
    g_diag.magic = 0x514C3735; g_diag.version = 1;
    g_diag.core_hz = SystemCoreClock; g_diag.stage = 4;
}

void Q_App_Poll(void)
{
    uint32_t now = HAL_GetTick();
    if (g_command == 1) Q_App_Init();
    /* Two seconds to inspect the zero-initialized maze after a reset.
     * First 3 episodes: visible individual actions. Then batch updates. */
    if (mode == MODE_TRAIN && now - start_tick >= 2000u) {
        uint32_t interval = g_agent.episodes < 3 ? 20u : 10u;
        if (now - step_tick >= interval) {
            step_tick = now;
            uint32_t batch = g_agent.episodes < 3 ? 1u : 32u;
            for (uint32_t n = 0; n < batch && g_agent.episodes < QL_TRAIN_EPISODES; ++n) {
                uint32_t cycles = DWT->CYCCNT;
                int ended = ql_train_step(&g_agent);
                update_cycles = DWT->CYCCNT - cycles;
                if (ended) {
                    uint32_t slot = (g_agent.episodes - 1) % 100;
                    recent_sum -= recent[slot]; recent[slot] = (uint8_t)g_agent.last_success;
                    recent_sum += recent[slot]; if (recent_count < 100) recent_count++;
                }
            }
            if (g_agent.episodes >= QL_TRAIN_EPISODES) {
                path_steps = ql_evaluate(&g_agent, path);
                mode = path_steps ? MODE_DEMO : MODE_UNSOLVED;
                demo_index = 0; step_tick = now;
                training_ms = now - start_tick;
            }
        }
    } else if (mode == MODE_DEMO && now - step_tick >= (demo_index == path_steps ? 1000u : 180u)) {
        step_tick = now;
        if (demo_index < path_steps) demo_index++;
        else { demo_index = 0; demo_runs++; }
    }
    uint32_t state = mode == MODE_DEMO ? path[demo_index] : g_agent.state;
    uint32_t rate = recent_count ? recent_sum * 100u / recent_count : 0;
    if (now - render_tick >= 100u) {
        render_tick = now;
        Q_Display_Render(mode, state, path, path_steps, rate, &display_stats);
    }
    if (now - led_tick >= 500u) { HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); led_tick = now; }
    /* A monitor retries if a read overlaps these stores. DTCM is uncached. */
    if (now - report_tick >= 100u) {
    report_tick = now;
    g_diag.sequence++; __DMB();
    g_diag.heartbeat_ms = HAL_GetTick(); g_diag.mode = mode;
    g_diag.episodes = g_agent.episodes; g_diag.successes = g_agent.successes;
    g_diag.timeouts = g_agent.timeouts; g_diag.total_steps = g_agent.total_steps;
    g_diag.state = state; g_diag.steps = g_agent.steps;
    g_diag.epsilon_permille = g_agent.epsilon_permille;
    g_diag.last_steps = g_agent.last_steps; g_diag.last_return = g_agent.last_return;
    g_diag.eval_steps = path_steps; g_diag.recent_success_percent = rate;
    g_diag.frames = display_stats.frames; g_diag.spi_errors = display_stats.spi_errors;
    g_diag.render_us = display_stats.render_us; g_diag.update_cycles = update_cycles;
    g_diag.training_ms = training_ms; g_diag.demo_runs = demo_runs;
    __DMB(); g_diag.sequence++;
    }
    HAL_Delay(1);
}
