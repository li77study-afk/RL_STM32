/**
 * @file q_learning.c
 * @brief 从零实现表格 Q-learning：环境、策略、TD 更新与无探索评估。
 *
 * 一次训练步骤的完整数据流：
 * 当前状态 s → ε-greedy 选择 a → 环境产生 (s',r,terminal)
 *            → 更新 Q(s,a) → 移动到 s' → 判断本回合是否结束。
 *
 * 这里没有神经网络，也没有反向传播。学习结果就是一个 float 二维数组，
 * 每次交互只更新其中一个元素；无需预先提供“正确动作”标签。
 */
#include "q_learning.h"
#include <string.h>

/* 环境从 g_current_map.cells 读取，使用前必须调用 Map_Init(1~100)。
 * 地图库元数据中的 shortest_steps 只供界面/测试对照，不参与选动作或更新。 */

/* xorshift32：仅用位移和异或就能生成可复现的伪随机序列。
 * 内部状态不能为 0，否则位移/异或后永远还是 0；ql_init() 会处理这种输入。
 * 每个智能体保存自己的随机数状态，因此不同测试实例之间互不影响。 */
static uint32_t random_u32(QLearner *a)
{
    uint32_t x = a->rng;
    /* 三次混合使用经典 xorshift32 的移位参数，运算按 uint32_t 位宽进行。 */
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return a->rng = x;
}

int ql_is_wall(uint32_t s)
{
    /* || 从左到右短路求值：编号越界时，不会执行右侧的数组访问。
     * 整除得到行号，取余得到列号。 */
    return s >= QL_STATES || g_current_map.cells[s / QL_SIDE][s % QL_SIDE] == '#';
}

QLTransition ql_transition(uint32_t s, uint32_t action)
{
    /* 先准备“原地不动、撞墙扣分”的默认结果，非法移动直接返回它。 */
    QLTransition t = {s, QL_REWARD_WALL, 0};
    /* 终点是吸收状态：已经在终点再调用时，不重复领取 +30 奖励。 */
    if (s == QL_GOAL) { t.reward = 0; t.terminal = 1; return t; }
    if (ql_is_wall(s) || action >= QL_ACTIONS) return t;
    /* 坐标使用有符号 int，才能正确表达从第 0 行向上走得到的 -1。 */
    int row = (int)(s / QL_SIDE), col = (int)(s % QL_SIDE);
    if (action == QL_UP) row--;
    else if (action == QL_RIGHT) col++;
    else if (action == QL_DOWN) row++;
    else col--;
    /* 必须先检查边界，再转换为无符号编号并访问地图。 */
    if (row < 0 || row >= (int)QL_SIDE || col < 0 || col >= (int)QL_SIDE) return t;
    uint32_t next = (uint32_t)row * QL_SIDE + (uint32_t)col;
    if (ql_is_wall(next)) return t;
    /* 只有没有越界、也没有撞墙时，才真正改变位置。 */
    t.next_state = next;
    t.terminal = next == QL_GOAL;
    t.reward = t.terminal ? QL_REWARD_GOAL : QL_REWARD_STEP;
    return t;
}

void ql_init(QLearner *a, uint32_t seed)
{
    /* 同时清零 Q 表、访问次数、回合计数和调试字段，相当于忘记旧经验。 */
    memset(a, 0, sizeof(*a));
    a->rng = seed ? seed : 1u;
    a->state = QL_START;
    a->epsilon_permille = 1000;
}

uint32_t ql_greedy_action(const QLearner *a, uint32_t s)
{
    /* argmax 返回动作下标，不是 Q 数值。严格使用 >，因此相等时不替换。
     * 固定的平局规则让评估路径可复现；训练选动作会另外随机处理平局。 */
    uint32_t best = 0;
    for (uint32_t k = 1; k < QL_ACTIONS; ++k)
        if (a->q[s][k] > a->q[s][best]) best = k;
    return best;
}

static uint32_t choose_action(QLearner *a)
{
    /* 把随机数映射到 0~999，再与 ε 的千分数比较。
     * 命中探索分支时，四个方向都可能被选中，包括撞墙方向；墙的代价由
     * 环境反馈给学习器，而不是在选动作前把它隐藏掉。
     * % 映射有很小的模偏差，对本教学实验不作精确随机分布要求。 */
    if (random_u32(a) % 1000u < a->epsilon_permille)
        return random_u32(a) % QL_ACTIONS;
    /* 利用分支：先找最高 Q 数值，再收集所有恰好等于它的动作。
     * 初始 Q 全为 0，四个方向都是候选；随机打破平局避免总选 UP。
     * 这里比较相等值是为了寻找并列最大项，不是近似浮点数值测试。
     * Q 值有限时至少有一个最大项，所以 count 不会为 0。 */
    uint32_t choices[QL_ACTIONS], count = 0;
    float best = a->q[a->state][ql_greedy_action(a, a->state)];
    for (uint32_t k = 0; k < QL_ACTIONS; ++k)
        if (a->q[a->state][k] == best) choices[count++] = k;
    return choices[random_u32(a) % count];
}

void ql_update(QLearner *a, uint32_t s, uint32_t action, QLTransition t)
{
    /* 贝尔曼最优关系给出的一步 TD 更新：
     * Q(s,a) ← Q(s,a) + α * [r + γ*max Q(s',a') - Q(s,a)]。
     *
     * 终点没有后续奖励，所以 future 必须为 0；即使终点那行 Q 表里误留了
     * 数值，也不能加进目标。普通转移则用下一状态的最大 Q 值估计未来。
     *
     * 这是 off-policy 的 Q-learning：采样动作可来自 ε-greedy，目标却始终
     * 假定未来选择最大 Q 的动作，而不是使用下一次实际探索选中的动作。
     * 1024 步上限只是截断采样，不是环境终点，因此最后一步仍可 bootstrap。 */
    float future = t.terminal ? 0.0f : a->q[t.next_state][ql_greedy_action(a, t.next_state)];
    /* 保存旧估计，再组合“即时奖励 + 折扣后的未来估计”。 */
    float old_q = a->q[s][action];
    float target = (float)t.reward + QL_GAMMA * future;
    /* 误差为正就提高此动作的 Q 值，为负就降低；α 控制本次改动幅度。 */
    float td_error = target - old_q;
    a->q[s][action] = old_q + QL_ALPHA * td_error;
    /* 调试器查看这些字段，可把一次更新完整代入公式；它们不参与选动作。 */
    a->last_state = s; a->last_action = action; a->last_next_state = t.next_state;
    a->last_reward = t.reward; a->last_old_q = old_q; a->last_target = target;
    a->last_td_error = td_error; a->last_new_q = a->q[s][action];
}

int ql_train_step(QLearner *a)
{
    /* 延迟到下一次调用再开新回合，让刚结束的状态和统计能被界面读取。
     * 只清空回合局部状态，保留 Q 表、随机数状态和累计经验。 */
    if (a->episode_finished) {
        a->state = QL_START; a->steps = 0; a->episode_return = 0;
        a->episode_finished = 0;
    }
    /* 以“已结束回合数”为依据衰减 ε，不随 CPU 速度或显示帧率改变。
     * QL_EPSILON_DECAY_EPISODES 回合前从 1000 降向 50，之后固定为 50；整数除法会产生取整。 */
    a->epsilon_permille = a->episodes >= QL_EPSILON_DECAY_EPISODES ? 50u :
        1000u - a->episodes * 950u / QL_EPSILON_DECAY_EPISODES;
    uint32_t action = choose_action(a);
    QLTransition t = ql_transition(a->state, action);
    /* 访问计数记录的是执行动作前的状态。先用旧状态更新，再修改当前位置，
     * 否则容易把 Q(s,a) 错写成 Q(s',a)。 */
    a->visits[a->state]++;
    ql_update(a, a->state, action, t);
    a->state = t.next_state;
    /* 屏幕 RETURN 是即时奖励之和。比如 20 步到终点且不撞墙：
     * 前 19 步各 -1，最后一步 +30，总和为 11。 */
    a->episode_return += t.reward;
    a->steps++; a->total_steps++;
    /* 达到终点或耗尽本回合步数，都会结束当前回合。
     * 如果恰好在第 1024 步到终点，terminal=1，仍计为成功而不是超时。 */
    if (t.terminal || a->steps >= QL_MAX_STEPS) {
        a->episodes++;
        a->successes += t.terminal;
        a->timeouts += !t.terminal;
        a->last_steps = a->steps; a->last_return = a->episode_return;
        a->last_success = t.terminal; a->episode_finished = 1;
        return 1;
    }
    return 0;
}

uint32_t ql_evaluate(const QLearner *a, uint8_t path[QL_STATES + 1])
{
    /* 使用 const 指针表达只读评估；不调用随机选动作或 Q 更新。
     * 这里只测试“当前学到的策略能否走通”，并不搜索或修复策略。 */
    uint8_t visited[QL_STATES] = {0};
    uint32_t s = QL_START;
    path[0] = (uint8_t)s;
    for (uint32_t n = 0; n < QL_STATES; ++n) {
        /* 环境和贪心平局规则都是确定的，重复进入同一状态后必定重复后续
         * 动作，形成循环。撞墙原地不动也是长度为 1 的循环。 */
        if (visited[s]) return 0;
        visited[s] = 1;
        QLTransition t = ql_transition(s, ql_greedy_action(a, s));
        /* 一次动作增加一个路径状态，所以 n 步需要 n+1 个存储位置。 */
        s = t.next_state; path[n + 1] = (uint8_t)s;
        if (t.terminal) return n + 1;
    }
    return 0;
}
