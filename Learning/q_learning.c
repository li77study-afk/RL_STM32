#include "q_learning.h"
#include <string.h>

/* '.' = free, '#' = wall. This map contains two routes and dead ends. */
const char ql_maze[QL_SIDE][QL_SIDE + 1] = {
    "....#...",
    "###.#.#.",
    "....#.#.",
    ".####.#.",
    "......#.",
    ".######.",
    "........",
    "######.."
};

static uint32_t random_u32(QLearner *a)
{
    uint32_t x = a->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return a->rng = x;
}

int ql_is_wall(uint32_t s)
{
    return s >= QL_STATES || ql_maze[s / QL_SIDE][s % QL_SIDE] == '#';
}

QLTransition ql_transition(uint32_t s, uint32_t action)
{
    QLTransition t = {s, QL_REWARD_WALL, 0};
    if (s == QL_GOAL) { t.reward = 0; t.terminal = 1; return t; }
    if (ql_is_wall(s) || action >= QL_ACTIONS) return t;
    int row = (int)(s / QL_SIDE), col = (int)(s % QL_SIDE);
    if (action == QL_UP) row--;
    else if (action == QL_RIGHT) col++;
    else if (action == QL_DOWN) row++;
    else col--;
    if (row < 0 || row >= (int)QL_SIDE || col < 0 || col >= (int)QL_SIDE) return t;
    uint32_t next = (uint32_t)row * QL_SIDE + (uint32_t)col;
    if (ql_is_wall(next)) return t;
    t.next_state = next;
    t.terminal = next == QL_GOAL;
    t.reward = t.terminal ? QL_REWARD_GOAL : QL_REWARD_STEP;
    return t;
}

void ql_init(QLearner *a, uint32_t seed)
{
    memset(a, 0, sizeof(*a));
    a->rng = seed ? seed : 1u;
    a->state = QL_START;
    a->epsilon_permille = 1000;
}

uint32_t ql_greedy_action(const QLearner *a, uint32_t s)
{
    uint32_t best = 0;
    for (uint32_t k = 1; k < QL_ACTIONS; ++k)
        if (a->q[s][k] > a->q[s][best]) best = k;
    return best;
}

static uint32_t choose_action(QLearner *a)
{
    if (random_u32(a) % 1000u < a->epsilon_permille)
        return random_u32(a) % QL_ACTIONS;
    /* Randomize exact ties during training to avoid an initial UP bias. */
    uint32_t choices[QL_ACTIONS], count = 0;
    float best = a->q[a->state][ql_greedy_action(a, a->state)];
    for (uint32_t k = 0; k < QL_ACTIONS; ++k)
        if (a->q[a->state][k] == best) choices[count++] = k;
    return choices[random_u32(a) % count];
}

void ql_update(QLearner *a, uint32_t s, uint32_t action, QLTransition t)
{
    /* Q(s,a) <- Q(s,a) + alpha * [r + gamma*max Q(s',a') - Q(s,a)]
     * A terminal goal has NO future reward: its target is just r.
     * The 256-step cap is a collection truncation, not a terminal state. */
    float future = t.terminal ? 0.0f : a->q[t.next_state][ql_greedy_action(a, t.next_state)];
    float old_q = a->q[s][action];
    float target = (float)t.reward + QL_GAMMA * future;
    float td_error = target - old_q;
    a->q[s][action] = old_q + QL_ALPHA * td_error;
    a->last_state = s; a->last_action = action; a->last_next_state = t.next_state;
    a->last_reward = t.reward; a->last_old_q = old_q; a->last_target = target;
    a->last_td_error = td_error; a->last_new_q = a->q[s][action];
}

int ql_train_step(QLearner *a)
{
    if (a->episode_finished) {
        a->state = QL_START; a->steps = 0; a->episode_return = 0;
        a->episode_finished = 0;
    }
    /* Linear schedule: 100% exploration initially, 5% after 1000 episodes. */
    a->epsilon_permille = a->episodes >= 1000u ? 50u : 1000u - a->episodes * 950u / 1000u;
    uint32_t action = choose_action(a);
    QLTransition t = ql_transition(a->state, action);
    a->visits[a->state]++;
    ql_update(a, a->state, action, t);
    a->state = t.next_state;
    a->episode_return += t.reward;
    a->steps++; a->total_steps++;
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
    uint8_t visited[QL_STATES] = {0};
    uint32_t s = QL_START;
    path[0] = (uint8_t)s;
    for (uint32_t n = 0; n < QL_STATES; ++n) {
        if (visited[s]) return 0;  /* Greedy cycle / collision: not solved. */
        visited[s] = 1;
        QLTransition t = ql_transition(s, ql_greedy_action(a, s));
        s = t.next_state; path[n + 1] = (uint8_t)s;
        if (t.terminal) return n + 1;
    }
    return 0;
}
