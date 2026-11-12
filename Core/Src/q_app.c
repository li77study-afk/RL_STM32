/**
 * @file q_app.c
 * @brief 把地图、Q-learning、屏幕和诊断组织成一个裸机主循环。
 *
 * 这里决定“什么时候训练、什么时候画图”，Learning/ 负责“怎样学习”。
 * 没有 RTOS：每次 Poll 做有限量工作，随后回到 main 的 while 循环。
 */
#include "q_app.h"
#include "main.h"
#include <string.h>

/* 链接脚本把 .diagnostics 放在 DTCM，避免 SWD 读到 AXI 数据 Cache 的旧副本。
 * NOLOAD 区不会被启动代码自动清零，所以 main/ql_init 必须显式初始化。
 * g_command=1 只表示重启同一张图的训练，不承担切图功能。 */
__attribute__((section(".diagnostics"), aligned(32))) volatile QDiagnostics g_diag;
__attribute__((section(".diagnostics"), aligned(32))) QLearner g_agent;
__attribute__((section(".diagnostics"), aligned(32))) volatile uint32_t g_command;

/* 256 个状态编号为 0~255，uint8_t 恰好容纳；路径数组另留一个起点位置。 */
_Static_assert(QL_STATES <= 256u, "Path element type must be widened above 256 states");
static uint8_t path[QL_STATES + 1], recent[100];
static uint32_t mode, path_steps, demo_index, recent_sum, recent_count;
static uint32_t start_tick, step_tick, render_tick, led_tick;
static uint32_t report_tick, update_cycles, training_ms, demo_runs;
static QDisplayStats display_stats;

void Q_App_Init(void)
{
    /* ==================== 你更换地图只需修改这里 ====================
     * Map_Init(1) 加载第 1 张图，Map_Init(42) 加载第 42 张图。
     * 有效编号 1~100。地图预览与编号位于 maps/README.md、maps/gallery.html。
     * 改完重新编译并烧录。该函数把 Flash 中的选定地图复制到当前地图 RAM。
     * 输入无效编号时返回 0，进入错误处理，避免在未初始化的环境里训练。
     */
    if (!Map_Init(1)) Error_Handler();

    /* 必须在换图后清空 Q 表：相同的状态编号在不同地图里可能有不同的墙和
     * 转移规则，旧图的经验不能直接当成新图的初始答案。 */
    ql_init(&g_agent, QL_SEED);
    memset(recent, 0, sizeof(recent));
    mode = MODE_TRAIN; path_steps = 0; demo_index = 0;
    recent_sum = 0; recent_count = 0;
    start_tick = HAL_GetTick(); step_tick = start_tick;
    /* 计时基准减去刷新周期，使第一次 Poll 就能显示零初始化画面。
     * uint32_t 差值按模 2^32 运算，允许跨越 HAL tick 的正常回绕。 */
    render_tick = start_tick - 100; report_tick = start_tick - 100;
    led_tick = start_tick;
    update_cycles = 0; training_ms = 0; demo_runs = 0;
    memset(&display_stats, 0, sizeof(display_stats));
    g_command = 0;
    g_diag.magic = 0x514C3735; g_diag.version = 2;
    g_diag.core_hz = SystemCoreClock; g_diag.stage = 4;
}

void Q_App_Poll(void)
{
    if (g_command == 1) Q_App_Init();
    /* 重启后再读取 now，确保它不会早于 Init 设置的新 start_tick。 */
    uint32_t now = HAL_GetTick();
    if (mode == MODE_TRAIN && now - start_tick >= 2000u) {
        /* 先留 2 秒看初始地图。第一回合单步显示，之后批量计算以缩短等待。
         * interval 是两批计算的最小间隔，实际还受 SPI 绘制阻塞时间影响。 */
        uint32_t interval = g_agent.episodes < 1 ? 5u : 2u;
        if (now - step_tick >= interval) {
            step_tick = now;
            uint32_t batch = g_agent.episodes < 1 ? 1u : 256u;
            for (uint32_t n = 0; n < batch && g_agent.episodes < QL_TRAIN_EPISODES; ++n) {
                /* DWT 只包住算法调用，不把屏幕发送和 HAL_Delay 算入训练步骤。 */
                uint32_t cycles = DWT->CYCCNT;
                int ended = ql_train_step(&g_agent);
                update_cycles = DWT->CYCCNT - cycles;
                if (ended) {
                    /* 环形数组维护最近 100 回合的成功率：先减掉被覆盖的旧值，
                     * 再放入本回合的 0/1。未满 100 回合时按实际样本数计算。 */
                    uint32_t slot = (g_agent.episodes - 1) % 100;
                    recent_sum -= recent[slot]; recent[slot] = (uint8_t)g_agent.last_success;
                    recent_sum += recent[slot]; if (recent_count < 100) recent_count++;
                }
            }
            if (g_agent.episodes >= QL_TRAIN_EPISODES) {
                /* 评估只读 Q 表，走通才播放结果；没有使用 BFS 参考答案补路。 */
                path_steps = ql_evaluate(&g_agent, path);
                mode = path_steps ? MODE_DEMO : MODE_UNSOLVED;
                demo_index = 0; step_tick = now;
                training_ms = HAL_GetTick() - start_tick;
            }
        }
    } else if (mode == MODE_DEMO && now - step_tick >= (demo_index == path_steps ? 1000u : 120u)) {
        /* 演示沿评估时生成的路径前进：每步 120 ms，终点停留 1 秒后重播。
         * 此分支不再调用 ql_train_step，因此模型保持不变。 */
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
    /* LED 每 500 ms 翻转一次，可辅助判断主循环仍在运行。 */
    if (now - led_tick >= 500u) { HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); led_tick = now; }
    if (now - report_tick >= 100u) {
        report_tick = now;
        /* 简单序列锁：偶数→奇数→写字段→偶数。__DMB 约束数据访问顺序。
         * 电脑在读取前后核对序号，变了就重试，避免跨越两个版本的快照。 */
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
        g_diag.map_id = g_current_map.id; g_diag.map_count = MAP_COUNT;
        g_diag.shortest_reference = g_current_map.shortest_steps;
        g_diag.free_cells = g_current_map.free_cells; g_diag.category = g_current_map.category;
        g_diag.training_target = QL_TRAIN_EPISODES; g_diag.grid_side = QL_SIDE;
        __DMB(); g_diag.sequence++;
    }
    HAL_Delay(1);
}
