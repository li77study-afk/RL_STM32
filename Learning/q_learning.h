/**
 * @file q_learning.h
 * @brief 表格 Q-learning 的数据结构、超参数和函数接口。
 *
 * 阅读顺序建议：本文件的数据结构 → ql_transition() 的环境规则 →
 * choose_action() 的选动作方法 → ql_update() 的更新公式 → ql_train_step()。
 *
 * 本模块仅依赖标准 C，不调用 HAL、不访问屏幕或寄存器，因此电脑测试与
 * STM32 固件可以编译同一份算法。调用者负责调度训练和显示结果。
 */
#ifndef Q_LEARNING_H
#define Q_LEARNING_H
#include <stdint.h>
#include "map_library.h"

/* 地图采用行优先编号：state = row * QL_SIDE + col。
 * 例如第 2 行、第 3 列（均从 0 编号）对应状态 2*16+3=35。
 * 墙也保留一个状态编号，但合法行走不会进入墙格。
 * 改地图尺寸时，需要同步修改状态数、起终点、显示布局和测试中的固定尺寸。 */
#define QL_SIDE MAP_SIDE            /* 地图库统一为 16 行、16 列。 */
#define QL_STATES (QL_SIDE * QL_SIDE) /* 状态数：16*16=256。 */
#define QL_ACTIONS 4u               /* 每个状态都可尝试上、右、下、左四种动作。 */
#define QL_START 0u                 /* 左上角起点：(row=0, col=0)。 */
#define QL_GOAL (QL_STATES - 1u)     /* 右下角终点：(row=15, col=15)，编号 255。 */
#define QL_MAX_STEPS 1024u           /* 单回合的采样上限，防止一直随机游走。 */
#define QL_TRAIN_EPISODES 5000u     /* 应用层在完成这么多回合后停止训练。 */
#define QL_EPSILON_DECAY_EPISODES 2000u /* 2000 回合内将探索率降至 5%。 */
#define QL_SEED 0x750u              /* 固定随机种子，便于重复实验；不是预存的答案。 */
#define QL_ALPHA 0.20f              /* 学习率 α：每次向 TD 目标靠近 20%。 */
#define QL_GAMMA 0.95f              /* 折扣因子 γ：越远的未来奖励权重越低。 */
#define QL_REWARD_STEP (-1)         /* 正常移动的代价，鼓励尽快到达目标。 */
#define QL_REWARD_WALL (-4)         /* 撞墙/越界的代价；位置保持不变。 */
#define QL_REWARD_GOAL 30           /* 进入终点的奖励，替代该步的 -1，不与 -1 相加。 */

/* 枚举值默认从 0 递增，可直接作为 Q 表列下标：
 * q[s][QL_UP] 表示“在状态 s 选择向上”的预期折扣回报。 */
typedef enum { QL_UP, QL_RIGHT, QL_DOWN, QL_LEFT } QLAction;
typedef struct {
    uint32_t next_state;           /* 执行动作后的位置 s'，撞墙时等于原位置 s。 */
    int32_t reward;                 /* 本次即时奖励 r，有负数，所以使用有符号类型。 */
    uint32_t terminal;              /* 1=已到终点、任务结束；0=任务仍可继续。 */
} QLTransition;
/* 一个智能体的全部可变状态。
 * Q 表是学习结果，其余成员用于随机数、回合统计、显示和调试。
 * ql_init() 会将整个结构清零，因此重启训练不会沿用上次 Q 值。 */
typedef struct {
    float q[QL_STATES][QL_ACTIONS]; /* 256*4*4=4096 字节；Q 值不是概率，也不是距离。 */
    uint32_t visits[QL_STATES];    /* 从每个状态执行过多少次动作，用于显示策略箭头。 */
    uint32_t rng,                  /* xorshift32 的内部状态，每次取随机数都会改变。 */
             state,                /* 当前训练位置。 */
             episodes,             /* 已完成的回合数，不包含正在进行的回合。 */
             steps,                /* 当前回合已经执行的动作数，撞墙也算一步。 */
             total_steps,          /* 自本次初始化以来的总交互/更新次数。 */
             successes,            /* 到达终点的回合数。 */
             timeouts;             /* 未到终点、因步数上限而截断的回合数。 */
    uint32_t episode_finished,     /* 回合结束标志；下次训练调用再重置位置和计数。 */
             epsilon_permille,     /* 探索概率的千分数：1000=100%，50=5%。 */
             last_steps,           /* 上一个已结束回合的步数。 */
             last_success;         /* 上一回合是否到达终点，取值为 0 或 1。 */
    int32_t episode_return,        /* 当前回合即时奖励直接求和，不做 γ 折扣。 */
            last_return;           /* 上一回合的奖励和；与 Q 值的折扣回报概念不同。 */
    /* 保存最近一次更新的中间值，便于在调试器里手算核对公式。 */
    uint32_t last_state,           /* 本次更新所对应的原状态 s。 */
             last_action,          /* 实际执行的动作 a，可能来自探索。 */
             last_next_state;      /* 环境返回的新状态 s'。 */
    int32_t last_reward;           /* 环境返回的即时奖励 r。 */
    float last_old_q,              /* 更新前的 Q(s,a)。 */
          last_target,             /* TD 目标：r + γ*max Q(s',a')，终点时只有 r。 */
          last_td_error,           /* TD 误差：target - old_q。 */
          last_new_q;              /* 更新后的 Q(s,a)。 */
} QLearner;

/* 当前环境由 Map_Init() 初始化，地图定义见 map_library.h。 */
/* 清空 Q 表/统计并设置随机种子；seed=0 会替换成 1。 */
void ql_init(QLearner *agent, uint32_t seed);
/* 越界编号也当作墙处理。 */
int ql_is_wall(uint32_t state);
/* 纯环境函数：给定 s、a，返回 s'、r、terminal，不修改智能体。 */
QLTransition ql_transition(uint32_t state, uint32_t action);
/* 返回最大 Q 值的动作下标；相等时保留最先找到的动作。state 必须合法。 */
uint32_t ql_greedy_action(const QLearner *agent, uint32_t state);
/* 对一个合法转移样本更新一个 Q 表元素；调用者保证状态和动作下标合法。 */
void ql_update(QLearner *agent, uint32_t state, uint32_t action, QLTransition t);
/* 执行一次交互和更新；该步结束回合时返回 1，否则返回 0。
 * 此函数不限制总回合数，5000 回合的停止条件由应用层负责。 */
int ql_train_step(QLearner *agent);
/* 从起点按贪心策略走到终点，返回动作数；失败返回 0。
 * path[0] 是起点，成功时有效元素为 path[0..返回值]，共“步数+1”个状态。
 * 失败时 path 仅包含尝试过程的前缀，不应作为完整路线播放。
 * 评估不探索、不更新 Q 表、不改变训练随机数状态。 */
uint32_t ql_evaluate(const QLearner *agent, uint8_t path[QL_STATES + 1]);
#endif
