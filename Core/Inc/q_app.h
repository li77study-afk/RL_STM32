#ifndef Q_APP_H
#define Q_APP_H
#include "q_learning.h"
/**
 * 应用层诊断快照，供 tools/monitor.py 通过 SWD 读取。
 * 每个成员都是 32 位，顺序必须与 Python 的 FIELDS 一致；变更布局需升级 version。
 * sequence 为偶数表示快照稳定，为奇数表示 MCU 正在写入，电脑端应重读。
 */
typedef struct {
    uint32_t sequence,             /* 写入前/后各加 1，防止读到半旧半新的数据。 */
             magic,                /* 固定标识 0x514C3735，用于确认固件类型。 */
             version,              /* 当前布局版本为 2（16×16 地图库版）。 */
             stage,                /* 1=时钟就绪，2=SPI 就绪，3=LCD 就绪，4=应用运行。 */
             fatal_count,          /* Error_Handler 被调用次数。 */
             heartbeat_ms;         /* 最近发布快照时的 HAL 毫秒计数。 */
    uint32_t mode,                 /* TRAIN / DEMO / UNSOLVED。 */
             episodes, successes, timeouts, total_steps, /* 训练累计统计。 */
             state,                /* 当前画面上的位置，演示时可与训练终点不同。 */
             steps;                /* 当前/最后训练回合的步数，不是演示步数。 */
    uint32_t epsilon_permille, last_steps;
    int32_t last_return;            /* 有符号奖励和，Python 需按补码解释。 */
    uint32_t eval_steps,            /* 无探索评估路径长度，0 代表尚未成功评估。 */
             frames, spi_errors,   /* 成功发送帧数和失败次数。 */
             update_cycles,        /* 最近一次训练步骤的 DWT 周期数。 */
             render_us;            /* 最近一帧绘制及传输耗时。 */
    uint32_t training_ms, recent_success_percent, demo_runs, core_hz;
    uint32_t map_id, map_count,     /* 当前地图编号、地图库总数。 */
             shortest_reference,   /* BFS 参考长度；算法更新不读取这个字段。 */
             free_cells, category, training_target, grid_side;
} QDiagnostics;
/* volatile 告诉编译器：这些数据可能被调试器观察或写入。
 * 它不替代内存屏障，也不解决 Cache 一致性；实体放在不可缓存的 DTCM。 */
extern volatile QDiagnostics g_diag;
extern QLearner g_agent;
extern volatile uint32_t g_command;
enum { MODE_TRAIN = 0, MODE_DEMO = 1, MODE_UNSOLVED = 2 };
/* 选图入口在此函数内部：Map_Init(1)。更换数字后重新编译烧录即可。 */
void Q_App_Init(void);
void Q_App_Poll(void);
/* 绘图函数只写局部统计，再由应用统一发布到 SWD 快照。 */
typedef struct { uint32_t frames, spi_errors, render_us; } QDisplayStats;
void Q_Display_Render(uint32_t mode, uint32_t state, const uint8_t *path,
                      uint32_t path_steps, uint32_t recent_success, QDisplayStats *stats);
#endif
